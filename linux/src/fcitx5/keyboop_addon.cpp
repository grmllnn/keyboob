/*
 * Keyboop Fcitx5 addon — Wayland-safe layout auto-switch for RU/EN.
 * Requires Fcitx5 headers at build time (fcitx5 package on Arch).
 */
#include "engine.hpp"
#include "layout_data.hpp"
#include "utf8.hpp"

#include <cstdlib>
#include <memory>
#include <string>
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
};

class KeyboopModule final : public AddonInstance {
  static constexpr char configFile[] = "conf/keyboop.conf";

public:
  explicit KeyboopModule(Instance *instance) : instance_(instance) {
    // Load dictionary data
    const char *env = std::getenv("KEYBOOP_DATA_DIR");
    std::string dataDir =
        env ? env : std::string(KEYBOOP_DEFAULT_DATA_DIR);
    // Installed path fallback
    if (!keyboop::LayoutData::shared().load(dataDir)) {
      keyboop::LayoutData::shared().load("/usr/share/keyboop");
    }

    factory_ = [this](InputContext &) {
      auto *st = new KeyboopState();
      applySettings(st->engine);
      return st;
    };
    instance_->inputContextManager().registerProperty("keyboopState",
                                                      &factory_);

    eventHandlers_.emplace_back(instance_->watchEvent(
        EventType::InputContextKeyEvent, EventWatcherPhase::PreInputMethod,
        [this](Event &event) { handleKey(static_cast<KeyEvent &>(event)); }));

    reloadConfig();
  }

  void reloadConfig() override { readAsIni(config_, configFile); }

  const Configuration *getConfig() const override { return &config_; }

  void setConfig(const RawConfig &config) override {
    config_.load(config, true);
    safeSaveAsIni(config_, configFile);
    // Refresh settings on all ICs is expensive; new key events pick up config_
    // via applySettings on demand.
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
    if (act.delete_count > 0) {
      // deleteSurroundingText: offset negative = before cursor, size in chars
      ic->deleteSurroundingText(-act.delete_count, act.delete_count);
    }
    if (!act.insert.empty())
      ic->commitString(act.insert);
    st->muted = false;
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

    // Manual hotkey
    if (key.checkKeyList(config_.manualHotkey.value())) {
      if (auto act = st->engine.manual_convert()) {
        applyReplace(ic, *act);
        keyEvent.filterAndAccept();
      }
      return;
    }

    // Backspace
    if (key.check(FcitxKey_BackSpace)) {
      st->engine.on_backspace();
      return; // let client handle
    }

    // Boundaries
    auto &cfg = st->engine.settings();
    if (key.check(FcitxKey_space) && cfg.on_space) {
      if (auto act = st->engine.on_boundary(" ")) {
        applyReplace(ic, *act);
        keyEvent.filterAndAccept();
      }
      // buffer already advanced inside on_boundary; let Space reach the app
      return;
    }
    if ((key.check(FcitxKey_Return) || key.check(FcitxKey_KP_Enter)) &&
        cfg.on_enter) {
      if (auto act = st->engine.on_boundary("\n")) {
        applyReplace(ic, *act);
        keyEvent.filterAndAccept();
      }
      return;
    }
    if (key.check(FcitxKey_Tab) && cfg.on_tab) {
      if (auto act = st->engine.on_boundary("\t")) {
        applyReplace(ic, *act);
        keyEvent.filterAndAccept();
      }
      return;
    }

    // Navigation / focus loss markers — clear buffer on arrows etc.
    if (key.check(FcitxKey_Left) || key.check(FcitxKey_Right) ||
        key.check(FcitxKey_Up) || key.check(FcitxKey_Down) ||
        key.check(FcitxKey_Home) || key.check(FcitxKey_End) ||
        key.check(FcitxKey_Prior) || key.check(FcitxKey_Next)) {
      st->engine.clear_context();
      return;
    }

    // Printable: derive character from key when possible.
    // Fcitx Key::key().sym() for latin; for composed text we rely on
    // forward — only track simple Unicode from keysym when length-1.
    if (key.hasModifier(KeyState::Ctrl) || key.hasModifier(KeyState::Alt) ||
        key.hasModifier(KeyState::Super))
      return;

    std::string ch;
    auto sym = key.sym();
    if (sym >= 0x20 && sym < 0x7f) {
      char c = static_cast<char>(sym);
      if (key.hasModifier(KeyState::Shift) && c >= 'a' && c <= 'z')
        c = static_cast<char>(c - 'a' + 'A');
      ch.assign(1, c);
    } else {
      // Non-ASCII: try key.key().toString() / utf8 from keysym via xkb — skip
      // complex for now; buffer may miss RU keys unless frontend gives us
      // surrounding text. Fallback: use key.rawKey display.
      // ponytail: use Key::keySymToUTF8 when available
      auto u = Key::keySymToUTF8(sym);
      if (!u.empty() && u != "\n" && u != "\t")
        ch = u;
    }

    if (ch.empty())
      return;

    st->engine.on_text(ch);
    if (auto act = st->engine.maybe_live_fix()) {
      applyReplace(ic, *act);
      // Don't filter the original key — we already replaced the word; swallow
      // the just-typed key since it's included in the conversion source.
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
