# Keyboop (Linux / Wayland & X11)

Автоматический переключатель раскладки клавиатуры (RU/EN) для Linux — нативный порт [Keyboop](https://keyboop.com).

Работает без прав суперпользователя (`root`), без прямого перехвата устройств ввода (`evdev grab`) и без постоянного чтения буфера обмена.

## Поддерживаемые окружения и архитектура

| Окружение | Бэкенд ввода | Как работает |
|---|---|---|
| **GNOME (Wayland / X11)** | **IBus engine** (по умолчанию) | Интегрируется как IBus-движок. Сохраняет системное переключение по `Super+Space`. Расширение GNOME Shell синхронизирует индикатор раскладки. |
| **KDE / Hyprland / Sway / wlroots** | **Fcitx5 addon** (опционально) | Модуль для Fcitx5, использующий API `deleteSurroundingText`. |

> **Терминалы:** Автоматическая конвертация в эмуляторах терминала отключена во избежание конфликтов. Ручная конвертация последнего набранного слова доступна по горячей клавише (**Ctrl+Alt+K** по умолчанию).

---

## Зависимости для сборки

Для сборки требуется компилятор с поддержкой **C++20**, **CMake** (>= 3.20), **Ninja** (или Make), **pkg-config**, **libxkbcommon**, **glib2** и заголовочные файлы выбранного метода ввода (**IBus** или **Fcitx5**).

### Debian / Ubuntu / Linux Mint / Pop!_OS (APT / deb)

```bash
sudo apt update
sudo apt install -y \
  build-essential \
  cmake \
  ninja-build \
  pkg-config \
  libxkbcommon-dev \
  libglib2.0-dev \
  libibus-1.0-dev \
  nlohmann-json3-dev

# Опционально: для Fcitx5 бэкенда
# sudo apt install -y libfcitx5core-dev extra-cmake-modules

# Опциональные утилиты для буфера обмена (PRIMARY selection fallback):
# sudo apt install -y wl-clipboard xclip
```

### Fedora / RHEL / AlmaLinux / Rocky Linux (RPM / DNF)

```bash
sudo dnf install -y \
  gcc-c++ \
  cmake \
  ninja-build \
  pkgconf-pkg-config \
  libxkbcommon-devel \
  glib2-devel \
  ibus-devel \
  json-devel

# Опционально: для Fcitx5 бэкенда
# sudo dnf install -y fcitx5-devel

# Опциональные утилиты для буфера обмена:
# sudo dnf install -y wl-clipboard xclip
```

### openSUSE (RPM / Zypper)

```bash
sudo zypper install -y \
  gcc-c++ \
  cmake \
  ninja \
  pkg-config \
  libxkbcommon-devel \
  glib2-devel \
  ibus-devel \
  nlohmann_json-devel

# Опционально: для Fcitx5 бэкенда
# sudo zypper install -y fcitx5-devel

# Опциональные утилиты:
# sudo zypper install -y wl-clipboard xclip
```

### Arch Linux / Manjaro / EndeavourOS (Pacman)

Установка зависимостей через pacman:

```bash
sudo pacman -S --needed \
  base-devel \
  cmake \
  ninja \
  pkgconf \
  libxkbcommon \
  glib2 \
  ibus \
  nlohmann-json

# Опционально: для Fcitx5 бэкенда
# sudo pacman -S --needed fcitx5

# Опциональные утилиты:
# sudo pacman -S --needed wl-clipboard xclip
```

Либо сборка и установка пакета целиком через `PKGBUILD`:

```bash
makepkg -si
```

### Nix / NixOS

#### Временное окружение разработки (Nix Shell)

```bash
nix-shell -p cmake ninja pkg-config gcc libxkbcommon glib ibus nlohmann_json
```

#### Декларативный `shell.nix`

```nix
{ pkgs ? import <nixpkgs> {} }:

pkgs.mkShell {
  nativeBuildInputs = with pkgs; [
    cmake
    ninja
    pkg-config
  ];
  buildInputs = with pkgs; [
    libxkbcommon
    glib
    ibus
    nlohmann_json
    # fcitx5 # раскомментируйте при сборке с Fcitx5
    # wl-clipboard # опционально
    # xclip        # опционально
  ];
}
```

Запуск: `nix-shell` -> `make`.

---

## Сборка и установка

### Вариант 1. Сборка для GNOME (IBus, по умолчанию)

```bash
# Сборка и прогон тестов
make

# Установка в систему (по умолчанию PREFIX=/usr)
sudo make install

# Обновление кэша IBus компонентов
ibus write-cache

# Включение IBus-движка Keyboop и установка расширения GNOME Shell
keyboopctl gnome-enable

# ВАЖНО: На Wayland GNOME Shell не может подгрузить новые расширения на лету.
# Завершите сеанс (Log Out) и войдите снова, либо перезагрузитесь.

# После повторного входа перезапустите демон IBus:
ibus restart

# Проверьте статус всех компонентов:
keyboopctl doctor
```

### Вариант 2. Сборка для KDE / Hyprland / Sway (Fcitx5)

```bash
# Сборка без IBus с включенным Fcitx5-модулем
make FCITX=ON IBUS=OFF

# Установка
sudo make install

# Перезапуск Fcitx5
fcitx5 -r -d

# Включите аддон Keyboop в fcitx5-configtool (если не активировался автоматически)
```

---

## Управление и настройка (`keyboopctl`)

Утилита командной строки `keyboopctl` управляет настройками и диагностирует состояние:

```bash
# Полная диагностика окружения, IBus/Fcitx, путей к данным и расширения GNOME
keyboopctl doctor

# Показать текущие источники ввода и обнаруженную пару раскладок (Latin ↔ Cyrillic)
keyboopctl layouts

# Включить / выключить автоконвертацию по нажатию пробела:
keyboopctl auto on
keyboopctl auto off

# Назначить комбинацию клавиш для ручной конвертации (по умолчанию Control+Alt+k):
keyboopctl hotkey Control+Alt+k

# Ручная конвертация слова через активную раскладку
keyboopctl convert ghbdtn    # Выведет: привет

# Проверка решения языкового детектора
keyboopctl decide ghbdtn     # Выведет: to-ru

# Откат системных источников ввода GNOME к стандартным xkb:
keyboopctl gnome-disable
```

Конфигурация пользователя сохраняется в файле `~/.config/keyboop/config` и применяется мгновенно при следующем нажатии клавиши.

---

## Принцип работы

1. **Определение пары раскладок:** Keyboop считывает системные источники ввода (в GNOME через GSettings `org.gnome.desktop.input-sources`, в Fcitx5 через текущую группу) и строит таблицу соответствия символов Latin ↔ Cyrillic с помощью `libxkbcommon`.
2. **Анализ текста:** При вводе слова формируется буфер. При нажатии разделителя (Space, Enter, Tab) движок анализирует n-граммы и словари.
3. **Замена текста:**
   - В IBus: заменяется слово в окружающем контексте (`surrounding text`), либо отправляется серия Backspace и новый текст, если приложение не отдает контекст.
   - В Fcitx5: вызывается `deleteSurroundingText`.
4. **Синхронизация индикатора GNOME:** Расширение Shell (`keyboop-switch@keyboop`) переключает активный источник ввода в интерфейсе GNOME при смене языка.

---

## Чек-лист проверки после установки (GNOME)

- [ ] Поле ввода GTK3/GTK4: наберите `ghbdtn` + Space → текст заменится на `привет `, индикатор раскладки переключится.
- [ ] Браузеры (Firefox, Chromium): автоконвертация работает, горячая клавиша `Ctrl+Alt+K` конвертирует выделенное или текущее слово.
- [ ] Qt-приложения (Kate, Telegram Desktop): корректная автоконвертация с первого нажатия пробела.
- [ ] Эмуляторы терминала (Ptyxis, GNOME Terminal): автоконвертация не мешает работе команд; по `Ctrl+Alt+K` конвертирует набранное слово.
- [ ] Поля паролей: конвертация отключена, символы не перехватываются.
- [ ] `keyboopctl doctor`: все проверки зелёные (`ok`).

---

## Лицензия

Проект распространяется под лицензией [MIT](LICENSE). Информация о сторонних компонентах доступна в [THIRD_PARTY.md](THIRD_PARTY.md).
