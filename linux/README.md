# Keyboop for Linux (Wayland)

C++20 port of the Keyboop layout engine as an **Fcitx5 module**.
Targets Arch Linux x86_64 on **GNOME / KDE Plasma / Hyprland** (Wayland).

No clipboard. No root. No raw evdev grab.
Text replace uses Fcitx5 `deleteSurroundingText` + `commitString`.

## Status

| Piece | State |
|-------|--------|
| Core (Keymap, LayoutDetector, buffer, engine) | builds + `ctest` |
| `AmbiguousPairs`, `UndoLearner` U2 observe/suppress | ported |
| `keyboopctl` | converts / decides offline |
| Fcitx5 addon | source ready; needs `fcitx5` packages to compile |
| Whisper / translate | not in this MVP |

### MVP gaps vs macOS (post-MVP)

- +30ms boundary delay / `completedOnly` race handling
- Inline / pause live-fix paths
- Modkey / double-tap hotkey modes in addon
- Group convert, selection convert, per-app rules
- Surrounding-text fallback for RU-layout key capture in addon
- Full characterization test matrix from core spec (snapshot JSON fixtures)

## Dependencies (Arch)

```bash
sudo pacman -S --needed base-devel cmake ninja nlohmann-json \
  fcitx5 fcitx5-configtool fcitx5-gtk fcitx5-qt \
  libxkbcommon pkgconf
```

## Build

```bash
cd linux
./build.sh
# or without Fcitx5: KEYBOOP_BUILD_FCITX=OFF ./build.sh
```

Without Fcitx5 headers the addon is skipped; core + tests still build:

```bash
cmake -B build -G Ninja -DKEYBOOP_BUILD_FCITX=OFF
```

Install (after Fcitx5 is present):

```bash
sudo cmake --install build
# or: makepkg -si  (see PKGBUILD)
```

## Config & hotkey

User file (created/updated by `fcitx5-configtool` or by hand):

`~/.config/fcitx5/conf/keyboop.conf`

```ini
[Keyboop]
Enabled=True
AutoEnabled=True
OnSpace=True
OnEnter=True
OnTab=True
LiveFix=True
ManualHotkey=Control+Alt+k
HotkeyMode=combo
DoubleTapTimeoutMs=250
```

Change the hotkey in **Fcitx5 Config Tool → Addons → Keyboop**, or edit the `KeyList` line.
Keys use XKB/Fcitx names (`Control+Alt+k`, `Alt_R`), not macOS keyCodes.

Reload without restart: save in configtool, or `fcitx5-remote -r`.

## Desktop setup (Fcitx5 required)

### GNOME Wayland
1. Install packages above + optional `gnome-shell-extension-kimpanel` (AUR) for candidate UI.
2. Autostart Fcitx5; set for XWayland apps: `XMODIFIERS=@im=fcitx`, `QT_IM_MODULE=fcitx`.
3. Prefer unset `GTK_IM_MODULE` so GTK uses Wayland text-input-v3.

### KDE Plasma Wayland
1. System Settings → Keyboard → Virtual Keyboard → **Fcitx 5**.
2. Do **not** force `GTK_IM_MODULE` / `QT_IM_MODULE` globally when using text-input.

### Hyprland
```ini
exec-once = fcitx5 -d -r
env = XMODIFIERS,@im=fcitx
# Qt on non-KWin: QT_IM_MODULE,fcitx
```

## Wayland app matrix (manual checklist)

Test after install. Mark pass/fail on your machine:

| App | GNOME | KDE | Hyprland |
|-----|-------|-----|----------|
| GTK4 (Text Editor / gedit) | | | |
| Qt6 (Kate / KWrite) | | | |
| Firefox | | | |
| Chromium / Electron (VS Code) | | | |
| Terminal (gnome-terminal / konsole / kitty) | | | |

**Unsupported by design:** lock screen, games that bypass IM, password fields (`Password`/`Sensitive` capability), apps that never talk to the input method.

## Offline tools

```bash
KEYBOOP_DATA_DIR=./data ./build/keyboopctl convert ghbdtn   # → привет
KEYBOOP_DATA_DIR=./data ./build/keyboopctl decide ghbdtn    # → to-ru
```

Global actions outside a text field (future `keyboopctl voice-toggle`) should be bound in GNOME Shortcuts / KDE / Hyprland `bind`, not inside the IM module.

## Layout

```
linux/
  src/core/       pure engine (no Fcitx)
  src/fcitx5/     addon + .conf descriptors
  src/ctl/        keyboopctl
  tests/          assert-based CTest
  data/           symlinks to Sources/Keyboop/Resources/*.json
  PKGBUILD
```

macOS Swift tree under `Sources/` stays the behaviour reference until Linux MVP is feature-complete.
