# Maintainer: Keyboop contributors
pkgname=keyboop
pkgver=0.1.0
pkgrel=1
pkgdesc="RU/EN layout auto-switch for Wayland (IBus on GNOME)"
arch=('x86_64')
url="https://github.com/grmllnn/keyboob"
license=('MIT')
depends=('ibus' 'libxkbcommon' 'gcc-libs' 'glibc' 'glib2')
makedepends=('cmake' 'ninja' 'nlohmann-json' 'pkgconf' 'ibus')
optdepends=(
  'fcitx5: optional addon (build with KEYBOOP_BUILD_FCITX=ON), not for GNOME'
  'wl-clipboard: PRIMARY selection fallback on Wayland (wl-paste)'
  'xclip: PRIMARY selection fallback on X11'
)
source=()

build() {
  cmake -B build -S "$startdir" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr \
    -DKEYBOOP_BUILD_IBUS=ON \
    -DKEYBOOP_BUILD_FCITX=OFF \
    -DKEYBOOP_BUILD_TESTS=ON
  cmake --build build
}

check() {
  ctest --test-dir build --output-on-failure
}

package() {
  DESTDIR="$pkgdir" cmake --install build
  install -Dm644 "$startdir/LICENSE" "$pkgdir/usr/share/licenses/$pkgname/LICENSE"
}
