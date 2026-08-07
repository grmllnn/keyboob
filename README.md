# Keyboop (Linux / Wayland)

RU/EN layout auto-switch — Linux port of [Keyboop](https://keyboop.com).
On **GNOME** it is an **IBus engine** (keeps Shell Super+Space). Optional
Fcitx5 module for desktops that already use Fcitx.

No clipboard. No root. No raw evdev grab.

## Status

| Piece | State |
|-------|--------|
| Core + xkb layout pairs | builds + `ctest` |
| IBus engine (GNOME) | default |
| Fcitx5 addon | optional (`make FCITX=ON`) |
| Whisper / translate | not ported |

## Dependencies

**Fedora:**

```bash
sudo dnf install cmake ninja-build gcc-c++ json-devel \
  libxkbcommon-devel ibus-devel glib2-devel
```

**Arch:**

```bash
sudo pacman -S --needed base-devel cmake ninja nlohmann-json \
  libxkbcommon ibus pkgconf
```

## Build

```bash
make                 # configure + build + ctest (IBus ON, Fcitx OFF)
make FCITX=ON        # also build Fcitx5 addon
sudo make install
# Do NOT overwrite keyboop.xml with `ibus-engine-keyboop --xml` —
# that file must stay a full <component>, engines are already inline.
ibus write-cache
keyboopctl gnome-enable
# gnome-enable also installs keyboop-switch@keyboop (GNOME Shell extension).
# Wayland cannot reload new extensions live — log out and back in once,
# then confirm:
#   gnome-extensions info keyboop-switch@keyboop   # State: ACTIVE
ibus restart
```

Without that extension, convert still rewrites text, but the panel/layout
stays on the old source (IBus alone is not enough on GNOME Wayland).

Disable / restore GNOME xkb sources:

```bash
keyboopctl gnome-disable
```

## How layouts work

Keyboop reads `org.gnome.desktop.input-sources`, builds a Latin↔Cyrillic
keymap pair via **xkbcommon** (not a hard-coded us/ru table), and registers
one IBus engine per source (`keyboop:us`, `keyboop:ru`, …). GNOME still
switches with Super+Space.

```bash
keyboopctl layouts          # show sources + detected pair
keyboopctl auto off         # disable auto-convert on space (Ctrl+Alt+K stays)
keyboopctl auto on
keyboopctl convert ghbdtn   # → привет (using active/xkb pair)
```

Dictionaries for auto-decide are still EN/RU (good enough for us/ru; weaker
for de/ua until more wordlists land).

Manual convert: **Ctrl+Alt+K** (same physical key on RU layout — matches `л` too).

## Fcitx5 (not for GNOME)

If you already run Fcitx on KDE/Hyprland:

```bash
make FCITX=ON IBUS=OFF
sudo make install
# enable addon in fcitx5-configtool; do NOT use on GNOME
```

## License

MIT — see `LICENSE` and `THIRD_PARTY.md`.
