# Keyboop (Linux / Wayland)

RU/EN layout auto-switch — Linux port of [Keyboop](https://keyboop.com).
On **GNOME** it is an **IBus engine** (keeps Shell Super+Space). Optional
Fcitx5 module for desktops that already use Fcitx.

No clipboard. No root. No raw evdev grab.

## Status

| Piece | State |
|-------|--------|
| Core + xkb layout pairs | builds + `ctest` |
| IBus engine (GNOME) | default; keys pass through, convert on Space/Enter/Tab |
| Fcitx5 addon | optional (`make FCITX=ON`); uses `deleteSurroundingText` |
| Whisper / translate | not ported |

Supported sessions: GNOME/IBus and KDE/Hyprland/Fcitx5 on Wayland or X11.
Environments without IBus or Fcitx5 are out of scope. Terminals: auto-convert
is off; **Ctrl+Alt+K** converts the tracked word only when surrounding text
at the caret matches it (never stacks on already-typed characters). Arch
GNOME/Wayland has no runtime smoke here — do not treat Arch as a verified
session until that check is run there.

## Dependencies

**Fedora:**

```bash
sudo dnf install cmake ninja-build gcc-c++ json-devel \
  libxkbcommon-devel ibus-devel glib2-devel
# optional: wl-clipboard (Wayland PRIMARY), xclip (X11 PRIMARY)
```

**Arch:**

```bash
sudo pacman -S --needed base-devel cmake ninja nlohmann-json \
  libxkbcommon ibus pkgconf
# optional: wl-clipboard, xclip
```

Without IBus headers, configure with `make IBUS=OFF` — a requested IBus/Fcitx
backend fails the build instead of silently skipping.

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
keyboopctl doctor    # session, IM, layouts, extension, paths
```

Without that extension, convert still rewrites text, but the panel/layout
stays on the old source (IBus alone is not enough on GNOME Wayland).

Disable / restore GNOME xkb sources:

```bash
keyboopctl gnome-disable
```

## How layouts work

Backend is chosen from session capabilities (desktop + IM modules), not from
the distro. On GNOME, Keyboop reads `org.gnome.desktop.input-sources`, builds
a Latin↔Cyrillic keymap pair via **xkbcommon**, and registers one IBus engine
per source (`keyboop:us`, `keyboop:ru`, …). GNOME still switches with
Super+Space. On Fcitx5, the pair and target IM come from the current Fcitx
group (`keyboard-us`, `keyboard-ru-phonetic`, …).

```bash
keyboopctl layouts          # show sources + detected pair
keyboopctl auto off         # disable auto-convert on space
keyboopctl auto on          # same file: ~/.config/keyboop/config
keyboopctl hotkey Control+Alt+k
# GNOME: extension prefs → auto toggle + shortcut button write that file too
keyboopctl convert ghbdtn   # → привет (using active/xkb pair)
keyboopctl doctor
```

Dictionaries for auto-decide are still EN/RU (good enough for us/ru; weaker
for de/ua until more wordlists land).

Manual convert: default **Ctrl+Alt+K** (same physical key on RU — `л` too).
Change it in extension prefs or `keyboopctl hotkey`. The current word is
replaced only if surrounding text at the caret matches it,
or via suffix Backspace when the app does not report surrounding text.
Otherwise it is a no-op, not a second insert. For other committed text it
uses a validated UTF-8 snapshot (IBus selection, or PRIMARY via `wl-paste` /
`xclip` only when it matches surrounding text). Stale PRIMARY is ignored.

## Fcitx5 (not for GNOME)

If you already run Fcitx on KDE/Hyprland:

```bash
make FCITX=ON IBUS=OFF
sudo make install
# enable addon in fcitx5-configtool; do NOT use on GNOME
```

## Manual GNOME app checklist

Unit tests and `keyboopctl` smoke do not drive a GUI session. After install,
on Fedora GNOME / IBus / Wayland, check by hand:

- [ ] GTK3 and GTK4 text fields: type `ghbdtn` + Space → `привет `; layout switches; no underline while typing
- [ ] Firefox: same auto-convert; Ctrl+Alt+K on a selected wrong-layout word
- [ ] Chromium or Electron: auto-convert; mid-phrase hotkey without eating neighbors
- [ ] Qt (Kate, Telegram, …): auto-convert on the **first** Space, not the second
- [ ] Terminal (GNOME Terminal / Ptyxis): typing is unchanged; Ctrl+Alt+K converts
- [ ] Password/PIN fields: no convert, no underline
- [ ] Super+Space mid-word: unfinished word stays as typed, not dropped
- [ ] `keyboopctl doctor`: session, IM, layouts, extension=ok, data path ok

## License

MIT — see `LICENSE` and `THIRD_PARTY.md`.
