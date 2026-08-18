# Keyboop — thin Make front-end over CMake.
# Usage: make | make test | sudo make install | make FCITX=ON | make IBUS=OFF
# A requested IBus/Fcitx backend fails configure if headers are missing.

BUILD ?= build
PREFIX ?= /usr
IBUS ?= ON
FCITX ?= OFF
CMAKE_BUILD_TYPE ?= Release
GENERATOR ?= $(shell command -v ninja >/dev/null 2>&1 && echo Ninja || echo "Unix Makefiles")

.PHONY: all configure build test install clean distclean deps

all: test

deps:
	@if command -v pacman >/dev/null 2>&1; then \
		sudo pacman -S --needed base-devel cmake ninja nlohmann-json libxkbcommon ibus pkgconf; \
	elif command -v dnf >/dev/null 2>&1; then \
		sudo dnf install -y cmake ninja-build gcc-c++ json-devel libxkbcommon-devel ibus-devel glib2-devel; \
	elif command -v apt-get >/dev/null 2>&1; then \
		sudo apt-get install -y cmake ninja-build build-essential nlohmann-json3-dev libxkbcommon-dev libibus-1.0-dev pkg-config; \
	else \
		echo "Unsupported package manager. Install dependencies manually." && exit 1; \
	fi

configure: $(BUILD)/CMakeCache.txt

$(BUILD)/CMakeCache.txt: CMakeLists.txt
	@command -v cmake >/dev/null 2>&1 || $(MAKE) deps
	@if [ -f "$(BUILD)/CMakeCache.txt" ]; then \
	  cached=$$(grep -E '^CMAKE_HOME_DIRECTORY:' "$(BUILD)/CMakeCache.txt" | cut -d= -f2-); \
	  if [ -n "$$cached" ] && [ "$$cached" != "$(CURDIR)" ]; then \
	    echo "stale CMake cache ($$cached ≠ $(CURDIR)) — wiping $(BUILD)"; \
	    rm -rf "$(BUILD)"; \
	  fi; \
	fi
	cmake -B "$(BUILD)" -G "$(GENERATOR)" \
	  -DCMAKE_BUILD_TYPE="$(CMAKE_BUILD_TYPE)" \
	  -DCMAKE_INSTALL_PREFIX="$(PREFIX)" \
	  -DKEYBOOP_BUILD_IBUS="$(IBUS)" \
	  -DKEYBOOP_BUILD_FCITX="$(FCITX)"

build: configure
	cmake --build "$(BUILD)"

test: build
	ctest --test-dir "$(BUILD)" --output-on-failure

install: build
	@cached=$$(grep -E '^CMAKE_INSTALL_PREFIX:PATH=' "$(BUILD)/CMakeCache.txt" | cut -d= -f2-); \
	if [ "$$cached" != "$(PREFIX)" ]; then \
	  echo "reconfigure install prefix: $$cached → $(PREFIX)"; \
	  cmake -B "$(BUILD)" -DCMAKE_INSTALL_PREFIX="$(PREFIX)"; \
	fi
	@cmake --install "$(BUILD)" || { \
	  echo ""; \
	  echo "install needs write access to $(PREFIX). Try:"; \
	  echo "  sudo make install"; \
	  exit 1; \
	}
	@echo "installed. Next:"
	@echo "  ibus write-cache && keyboopctl gnome-enable && ibus restart"

clean:
	@cmake --build "$(BUILD)" --target clean 2>/dev/null || true

distclean:
	rm -rf "$(BUILD)"
