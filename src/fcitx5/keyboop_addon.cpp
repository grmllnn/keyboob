/*
 * Keyboop Fcitx5 addon — Wayland-safe layout auto-switch for RU/EN.
 * Requires Fcitx5 headers at build time (fcitx5-devel).
 */
#include "engine.hpp"
#include "layout_data.hpp"
#include "utf8.hpp"

#include <chrono>
#include <cstdlib>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <fcitx-config/configuration.h>
#include <fcitx-config/enum.h>
#include <fcitx-config/iniparser.h>
#include <fcitx-config/option.h>
#include <fcitx-utils/i18n.h>
#include <fcitx-utils/key.h>
#include <fcitx-utils/keysym.h>
#include <fcitx/addonfactory.h>
#include <fcitx/addoninstance.h>
#include <fcitx/addonmanager.h>
#include <fcitx/event.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputcontextproperty.h>
#include <fcitx/inputmethodmanager.h>
#include <fcitx/instance.h>

namespace fcitx {

namespace {

FCITX_CONFIGURATION(
    KeyboopConfig,
    Option<bool> enabled{this, "Enabled", _("Enable Keyboop"), true};
    Option<bool> autoEnabled{this, "AutoEnabled", _("Auto convert on word boundary"),
                             true};
    Option<bool> onSpace{this, "OnSpace", _("Trigger on Space"), true};
    Option<bool> onEnter{this, "OnEnter", _("Trigger on Enter"), true};
    Option<bool> onTab{this, "OnTab", _("Trigger on Tab"), true};
    Option<bool> liveFix{this, "LiveFix", _("Live fix mid-word"), true};
    Option<bool> switchLayout{this, "SwitchLayout",
                              _("Switch IM layout after convert"), true};
    KeyListOption manualHotkey{
        this, "ManualHotkey", _("Manual convert hotkey"),
        {Key("Control+Alt+k")}, KeyListConstrain()};
    Option<std::string> hotkeyMode{this, "HotkeyMode",
                                   _("Hotkey mode: combo|modkey|key|doubletap"),
                                   "combo"};
    Option<int, IntConstrain> doubleTapTimeoutMs{
        this, "DoubleTapTimeoutMs", _("Double-tap timeout (ms)"), 250,
        IntConstrain(50, 2000)};);

class KeyboopState : public InputContextProperty {
public:
  keyboop::Engine engine;
  bool muted = false; // ignore our own synthetic commits
  uint64_t last_hotkey_us = 0;
};

bool can_replace(InputContext *ic) {
  return ic->capabilityFlags().test(CapabilityFlag::SurroundingText);
}

// Engine delete_count may include pending boundary chars not yet on screen.
// Snippet actions do NOT include them — only strip when counts match.
void strip_pending_boundary(keyboop::ReplaceAction &act, std::string_view ws,
                            bool keep_ws_in_insert) {
  if (ws.empty() || act.insert.size() < ws.size() || !act.insert.ends_with(ws))
    return;
  const int n = static_cast<int>(keyboop::utf8_length(ws));
  const int insert_wo =
      static_cast<int>(keyboop::utf8_length(act.insert) - n);
  if (act.delete_count == insert_wo + n)
    act.delete_count = insert_wo;
  if (!keep_ws_in_insert)
    act.insert.resize(act.insert.size() - ws.size());
}

void strip_pending_char(keyboop::ReplaceAction &act, std::string_view ch) {
  const int n = static_cast<int>(keyboop::utf8_length(ch));
  if (n > 0 && act.delete_count >= n)
    act.delete_count -= n;
}

bool name_looks_cyrillic(std::string_view name) {
  return name.find("ru") != std::string_view::npos ||
         name.find("russian") != std::string_view::npos ||
         name.find("ukrain") != std::string_view::npos ||
         name.find("belar") != std::string_view::npos ||
         name.find("by") != std::string_view::npos ||
         name.find("ua") != std::string_view::npos;
}

bool name_looks_latin(std::string_view name) {
  return name.find("us") != std::string_view::npos ||
         name.find("en") != std::string_view::npos ||
         name.find("gb") != std::string_view::npos ||
         name.find("intl") != std::string_view::npos;
}

class KeyboopModule final : public AddonInstance {
  static constexpr char configFile[] = "conf/keyboop.conf";

public:
  explicit KeyboopModule(Instance *instance)
      : instance_(instance),
        factory_([this](InputContext &) {
          auto *st = new KeyboopState();
          applySettings(st->engine);
          return st;
        }) {
    const char *env = std::getenv("KEYBOOP_DATA_DIR");
    std::string dataDir = env ? env : std::string(KEYBOOP_DEFAULT_DATA_DIR);
    if (!keyboop::LayoutData::shared().load(dataDir))
      keyboop::LayoutData::shared().load("/usr/share/keyboop");

    instance_->inputContextManager().registerProperty("keyboopState",
                                                      &factory_);

    eventHandlers_.emplace_back(instance_->watchEvent(
        EventType::InputContextKeyEvent, EventWatcherPhase::PreInputMethod,
        [this](Event &event) { handleKey(static_cast<KeyEvent &>(event)); }));

    eventHandlers_.emplace_back(instance_->watchEvent(
        EventType::InputContextFocusOut, EventWatcherPhase::Default,
        [this](Event &event) {
          auto &e = static_cast<InputContextEvent &>(event);
          if (auto *st = e.inputContext()->propertyFor(&factory_))
            st->engine.clear_context();
        }));

    eventHandlers_.emplace_back(instance_->watchEvent(
        EventType::InputContextReset, EventWatcherPhase::Default,
        [this](Event &event) {
          auto &e = static_cast<InputContextEvent &>(event);
          if (auto *st = e.inputContext()->propertyFor(&factory_))
            st->engine.clear_context();
        }));

    reloadConfig();
  }

  void reloadConfig() override { readAsIni(config_, configFile); }

  const Configuration *getConfig() const override { return &config_; }

  void setConfig(const RawConfig &config) override {
    config_.load(config, true);
    safeSaveAsIni(config_, configFile);
    // Live IC engines pick up config_ on next key via applySettings.
  }

private:
  void applySettings(keyboop::Engine &eng) {
    auto &s = eng.settings();
    s.enabled = *config_.enabled;
    s.auto_enabled = *config_.autoEnabled;
    s.on_space = *config_.onSpace;
    s.on_enter = *config_.onEnter;
    s.on_tab = *config_.onTab;
    s.live_fix = *config_.liveFix;
    s.hotkey_mode = *config_.hotkeyMode;
    s.double_tap_timeout_ms = *config_.doubleTapTimeoutMs;
    s.manual_hotkeys.clear();
    for (const auto &k : config_.manualHotkey.value())
      s.manual_hotkeys.push_back(k.toString());
  }

  void applyReplace(InputContext *ic, const keyboop::ReplaceAction &act) {
    if (act.delete_count <= 0 && act.insert.empty())
      return;
    auto *st = ic->propertyFor(&factory_);
    st->muted = true;
    if (act.delete_count > 0)
      ic->deleteSurroundingText(-act.delete_count, act.delete_count);
    if (!act.insert.empty())
      ic->commitString(act.insert);
    st->muted = false;
  }

  void maybeSwitchLayout(bool to_cyrillic) {
    if (!*config_.switchLayout)
      return;
    auto &imm = instance_->inputMethodManager();
    const auto &group = imm.currentGroup();
    std::string current;
    if (auto *mic = instance_->mostRecentInputContext())
      current = instance_->inputMethod(mic);
    std::string best;
    for (const auto &item : group.inputMethodList()) {
      const std::string &name = item.name();
      if (name == current)
        continue;
      if (to_cyrillic && name_looks_cyrillic(name)) {
        best = name;
        break;
      }
      if (!to_cyrillic && name_looks_latin(name) && !name_looks_cyrillic(name)) {
        best = name;
        break;
      }
    }
    if (!best.empty())
      instance_->setCurrentInputMethod(best);
  }

  bool tryManual(InputContext *ic, KeyboopState *st) {
    if (!can_replace(ic))
      return false;
    if (auto act = st->engine.manual_convert()) {
      applyReplace(ic, *act);
      maybeSwitchLayout(act->switch_to_cyrillic);
      return true;
    }
    return false;
  }

  bool isManualHotkey(const Key &key, KeyboopState *st) {
    const std::string mode = *config_.hotkeyMode;
    if (mode == "doubletap") {
      if (!key.checkKeyList(config_.manualHotkey.value()))
        return false;
      const uint64_t now = nowMicros();
      const uint64_t win =
          static_cast<uint64_t>(*config_.doubleTapTimeoutMs) * 1000ull;
      const bool hit =
          st->last_hotkey_us != 0 && now >= st->last_hotkey_us &&
          (now - st->last_hotkey_us) <= win;
      st->last_hotkey_us = now;
      return hit;
    }
    // combo | key | modkey — KeyList match (modkey alone needs release tracking;
    // ponytail: treat modkey like combo until a dedicated release watcher exists)
    return key.checkKeyList(config_.manualHotkey.value());
  }

  static uint64_t nowMicros() {
    using clock = std::chrono::steady_clock;
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            clock::now().time_since_epoch())
            .count());
  }

  void handleBoundary(KeyEvent &keyEvent, InputContext *ic, KeyboopState *st,
                      std::string_view ws, bool let_key_through) {
    if (!can_replace(ic)) {
      // Can't deleteSurroundingText here — keep buffer in sync only.
      st->engine.buffer().boundary(ws);
      return;
    }
    if (auto act = st->engine.on_boundary(ws)) {
      strip_pending_boundary(*act, ws, /*keep_ws_in_insert=*/!let_key_through);
      applyReplace(ic, *act);
      maybeSwitchLayout(act->switch_to_cyrillic);
      if (!let_key_through)
        keyEvent.filterAndAccept();
    }
  }

  void handleKey(KeyEvent &keyEvent) {
    if (keyEvent.isRelease())
      return;
    auto *ic = keyEvent.inputContext();
    if (!ic || ic->capabilityFlags().test(CapabilityFlag::Password) ||
        ic->capabilityFlags().test(CapabilityFlag::Sensitive))
      return;

    auto *st = ic->propertyFor(&factory_);
    if (st->muted)
      return;

    applySettings(st->engine);
    if (!st->engine.settings().enabled)
      return;

    const Key &key = keyEvent.key();

    if (isManualHotkey(key, st)) {
      if (tryManual(ic, st))
        keyEvent.filterAndAccept();
      return;
    }

    if (key.check(FcitxKey_BackSpace)) {
      st->engine.on_backspace();
      return;
    }

    if (key.check(FcitxKey_Escape)) {
      st->engine.clear_context();
      return;
    }

    auto &cfg = st->engine.settings();
    if (key.check(FcitxKey_space) && cfg.on_space) {
      handleBoundary(keyEvent, ic, st, " ", /*let_key_through=*/false);
      return;
    }
    if ((key.check(FcitxKey_Return) || key.check(FcitxKey_KP_Enter)) &&
        cfg.on_enter) {
      // Let Enter reach the app (chat send / newline) after we fix the word.
      handleBoundary(keyEvent, ic, st, "\n", /*let_key_through=*/true);
      return;
    }
    if (key.check(FcitxKey_Tab) && cfg.on_tab) {
      // Tab often moves focus — don't swallow it as a literal \t.
      handleBoundary(keyEvent, ic, st, "\t", /*let_key_through=*/true);
      return;
    }

    if (key.check(FcitxKey_Left) || key.check(FcitxKey_Right) ||
        key.check(FcitxKey_Up) || key.check(FcitxKey_Down) ||
        key.check(FcitxKey_Home) || key.check(FcitxKey_End) ||
        key.check(FcitxKey_Prior) || key.check(FcitxKey_Next) ||
        key.check(FcitxKey_Delete)) {
      st->engine.clear_context();
      return;
    }

    if (key.states().testAny(
            KeyStates{KeyState::Ctrl, KeyState::Alt, KeyState::Super}))
      return;

    std::string ch;
    const auto sym = key.sym();
    if (sym >= 0x20 && sym < 0x7f) {
      char c = static_cast<char>(sym);
      if (key.states().test(KeyState::Shift) && c >= 'a' && c <= 'z')
        c = static_cast<char>(c - 'a' + 'A');
      ch.assign(1, c);
    } else {
      auto u = Key::keySymToUTF8(sym);
      if (!u.empty() && u != "\n" && u != "\t" && u != "\r")
        ch = std::move(u);
    }

    if (ch.empty())
      return;

    st->engine.on_text(ch);
    if (!cfg.live_fix || !can_replace(ic))
      return;

    if (auto act = st->engine.maybe_live_fix()) {
      // Current key not committed yet — it's in the buffer/insert, not on screen.
      strip_pending_char(*act, ch);
      applyReplace(ic, *act);
      maybeSwitchLayout(act->switch_to_cyrillic);
      keyEvent.filterAndAccept();
    }
  }

  Instance *instance_;
  KeyboopConfig config_;
  FactoryFor<KeyboopState> factory_;
  std::vector<std::unique_ptr<HandlerTableEntry<EventHandler>>> eventHandlers_;
};

class KeyboopModuleFactory : public AddonFactory {
public:
  AddonInstance *create(AddonManager *manager) override {
    return new KeyboopModule(manager->instance());
  }
};

} // namespace

} // namespace fcitx

FCITX_ADDON_FACTORY_V2(keyboop, fcitx::KeyboopModuleFactory);
