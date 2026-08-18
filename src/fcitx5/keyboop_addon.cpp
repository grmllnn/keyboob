/*
 * Keyboop Fcitx5 addon — Wayland-safe layout auto-switch.
 * Requires Fcitx5 headers at build time (fcitx5-devel).
 */
#include "active_keymap.hpp"
#include "engine.hpp"
#include "layout_data.hpp"
#include "user_settings.hpp"
#include "utf8.hpp"
#include "xkb_pair.hpp"

#include <chrono>
#include <cstdlib>
#include <fstream>
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
  bool muted = false;
  uint64_t last_hotkey_us = 0;
};

bool can_replace(InputContext *ic) {
  return ic->capabilityFlags().test(CapabilityFlag::SurroundingText);
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
    keyboop::LayoutData::shared().load_from_search_path();

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
    auto us = keyboop::load_user_settings();
    {
      std::ifstream in(keyboop::user_config_path());
      if (in)
        s.auto_enabled = us.auto_enabled;
    }
    if (!us.hotkey.empty())
      s.manual_hotkeys.push_back(us.hotkey);
    else {
      for (const auto &k : config_.manualHotkey.value())
        s.manual_hotkeys.push_back(k.toString());
    }
  }

  void refreshKeymap() {
    auto &imm = instance_->inputMethodManager();
    const auto &group = imm.currentGroup();
    std::vector<keyboop::XkbLayoutId> layouts;
    for (const auto &item : group.inputMethodList()) {
      auto id = keyboop::parse_fcitx_im_name(item.name());
      if (!id.empty())
        layouts.push_back(std::move(id));
    }
    keyboop::XkbLayoutId active;
    if (auto *mic = instance_->mostRecentInputContext())
      active = keyboop::parse_fcitx_im_name(instance_->inputMethod(mic));
    auto pair = keyboop::pick_latin_cyrillic_pair(
        layouts, active.empty() ? nullptr : &active);
    if (pair.ok)
      keyboop::ActiveKeymap::shared().use_pair(std::move(pair));
  }

  bool applyReplace(InputContext *ic, const keyboop::ReplaceAction &act,
                    std::string_view pending_uncommitted = {}) {
    if (act.expected.empty() && act.replacement.empty())
      return false;
    std::string expect = act.expected;
    if (!pending_uncommitted.empty() &&
        expect.size() >= pending_uncommitted.size() &&
        expect.ends_with(pending_uncommitted)) {
      expect.resize(expect.size() - pending_uncommitted.size());
    }
    if (!keyboop::utf8_valid(expect) || !keyboop::utf8_valid(act.replacement))
      return false;

    const int n = static_cast<int>(keyboop::utf8_length(expect));
    if (can_replace(ic) && n > 0) {
      const auto &stxt = ic->surroundingText();
      auto match = keyboop::match_expected_at_caret(stxt.text(), stxt.cursor(),
                                                    expect);
      if (!match.ok)
        return false;
    }

    auto *st = ic->propertyFor(&factory_);
    st->muted = true;
    if (n > 0)
      ic->deleteSurroundingText(-n, n);
    if (!act.replacement.empty())
      ic->commitString(act.replacement);
    st->muted = false;
    return true;
  }

  void maybeSwitchLayout(bool to_cyrillic) {
    if (!*config_.switchLayout)
      return;
    refreshKeymap();
    auto &km = keyboop::ActiveKeymap::shared();
    const auto &id =
        to_cyrillic ? km.cyrillic_layout() : km.latin_layout();
    auto &imm = instance_->inputMethodManager();
    std::vector<std::string> names;
    for (const auto &item : imm.currentGroup().inputMethodList())
      names.push_back(item.name());
    auto best = keyboop::fcitx_im_for_layout(names, id);
    if (!best.empty())
      instance_->setCurrentInputMethod(best);
  }

  bool tryManual(InputContext *ic, KeyboopState *st) {
    if (!can_replace(ic))
      return false;
    refreshKeymap();
    if (auto act = st->engine.manual_convert()) {
      if (applyReplace(ic, *act)) {
        maybeSwitchLayout(act->switch_to_cyrillic);
        return true;
      }
    }
    return false;
  }

  bool isManualHotkey(const Key &key, KeyboopState *st) {
    auto us = keyboop::load_user_settings();
    const bool fileHit =
        !us.hotkey.empty() && key.check(Key(us.hotkey.c_str()));
    const std::string mode = *config_.hotkeyMode;
    if (mode == "doubletap") {
      if (!fileHit && !key.checkKeyList(config_.manualHotkey.value()))
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
    if (fileHit)
      return true;
    return key.checkKeyList(config_.manualHotkey.value());
  }

  static uint64_t nowMicros() {
    using clock = std::chrono::steady_clock;
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            clock::now().time_since_epoch())
            .count());
  }

  void handleBoundary(InputContext *ic, KeyboopState *st, std::string_view ws) {
    if (st->engine.buffer().current_word().empty())
      return;
    if (!can_replace(ic)) {
      st->engine.buffer().boundary(ws);
      return;
    }
    refreshKeymap();
    if (auto act = st->engine.on_boundary(ws)) {
      if (applyReplace(ic, *act))
        maybeSwitchLayout(act->switch_to_cyrillic);
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
      handleBoundary(ic, st, " ");
      return;
    }
    if ((key.check(FcitxKey_Return) || key.check(FcitxKey_KP_Enter)) &&
        cfg.on_enter) {
      handleBoundary(ic, st, "\n");
      return;
    }
    if (key.check(FcitxKey_Tab) && cfg.on_tab) {
      handleBoundary(ic, st, "\t");
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
      if (applyReplace(ic, *act, ch)) {
        maybeSwitchLayout(act->switch_to_cyrillic);
        keyEvent.filterAndAccept();
      }
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
