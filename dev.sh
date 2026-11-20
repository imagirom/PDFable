#!/usr/bin/env bash
# PDFable development script for WSL (Ubuntu) and native Linux.
#
#   ./dev.sh setup       install build tools, Qt 6.8.3 and MuPDF 1.28.6 (once)
#   ./dev.sh packages    only the apt packages (run as root: wsl -u root ./dev.sh packages)
#   ./dev.sh build       build the Linux version (and the self-test)
#   ./dev.sh test        run the headless self-test
#   ./dev.sh run [pdf]   start PDFable (WSLg shows the window on Windows)
#   ./dev.sh creator     start the PDF creator
#   ./dev.sh appimage    package dist/PDFable-<version>-x86_64.AppImage
#   ./dev.sh windows     build the Windows version with Visual Studio (WSL only)
#                        and package dist/PDFable-<version>-windows-x64.zip
#   ./dev.sh all         build, test, appimage and windows
#   ./dev.sh clean       remove the Linux build folder
#
# Dependencies go to ~/.local/share/pdfable-deps, the Linux build to
# ~/.cache/pdfable (Linux file system: much faster than building on /mnt/c).
# Overrides: QT_DIR=<Qt prefix> (e.g. /usr for the distribution's Qt 6), JOBS=<n>.
set -euo pipefail

QT_VERSION=6.8.3
MUPDF_VERSION=1.28.6

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DEPS="${PDFABLE_DEPS:-$HOME/.local/share/pdfable-deps}"
BUILD="${PDFABLE_BUILD:-$HOME/.cache/pdfable/build-linux}"
QT_DIR="${QT_DIR:-$DEPS/qt/$QT_VERSION/gcc_64}"
MUPDF_DIR="$DEPS/mupdf-$MUPDF_VERSION"
JOBS="${JOBS:-$(nproc)}"
LOGS="$ROOT/logs"      # inside the project folder so the logs can be read from Windows too
mkdir -p "$LOGS"

say() { printf '\033[1;31m==>\033[0m %s\n' "$*"; }
die() { printf '\033[1;31merror:\033[0m %s\n' "$*" >&2; exit 1; }
is_wsl() { grep -qi microsoft /proc/version 2>/dev/null; }
version() { sed -n 's/^project(PDFable VERSION \([0-9.]*\).*/\1/p' "$ROOT/CMakeLists.txt"; }

# ---------------------------------------------------------------- setup
APT_PACKAGES=(build-essential cmake ninja-build git pkg-config python3-venv python3-pip \
        curl rsync zip unzip file patchelf \
        libgl1-mesa-dev libegl1-mesa-dev libglib2.0-0 libfontconfig1 libfreetype6 libdbus-1-3 \
        libxkbcommon0 libxkbcommon-x11-0 libxcb-cursor0 libxcb-icccm4 libxcb-image0 libxcb-keysyms1 \
        libxcb-randr0 libxcb-render-util0 libxcb-shape0 libxcb-xinerama0 libxcb-xkb1 libxcb-xfixes0 \
        libxcb-sync1 libxcb-shm0 libx11-xcb1 libxrender1 libxi6 libsm6 libice6 libwayland-client0 \
        libwayland-cursor0 libwayland-egl1)

# A stamp records the installed package list (names differ slightly between Ubuntu releases,
# so asking dpkg for each name is not reliable). It lives outside $HOME because the install
# may run as root (wsl -u root) while the build runs as the normal user.
APT_STAMP=/var/local/pdfable-packages.stamp
packages_missing() { [[ "$(cat "$APT_STAMP" 2>/dev/null)" != "${APT_PACKAGES[*]}" ]]; }

setup_packages() {
    if ! packages_missing; then say "Build tools and libraries: installed"; return; fi
    say "Installing build tools and libraries"
    local sudo=sudo
    [[ $(id -u) -eq 0 ]] && sudo=
    $sudo apt-get update -q
    DEBIAN_FRONTEND=noninteractive $sudo apt-get install -y -q "${APT_PACKAGES[@]}"
    echo "${APT_PACKAGES[*]}" | $sudo tee "$APT_STAMP" >/dev/null
}

setup_qt() {
    if [[ -x "$QT_DIR/bin/qmake" || -x "$QT_DIR/bin/qmake6" ]]; then say "Qt: $QT_DIR"; return; fi
    say "Downloading Qt $QT_VERSION (about 300 MB, once)"
    python3 -m venv "$DEPS/venv"
    "$DEPS/venv/bin/pip" install -q --upgrade aqtinstall
    local tries=0
    until "$DEPS/venv/bin/aqt" install-qt linux desktop "$QT_VERSION" linux_gcc_64 -O "$DEPS/qt"; do
        tries=$((tries + 1)); [[ $tries -ge 3 ]] && die "Qt download failed 3 times (mirror trouble?) - run ./dev.sh setup again"
        say "Download interrupted, retrying ($tries/3)"
    done
}

setup_mupdf() {
    if [[ -f "$MUPDF_DIR/lib/libmupdf.a" ]]; then say "MuPDF: $MUPDF_DIR"; return; fi
    local src="$DEPS/src/mupdf-$MUPDF_VERSION-source"
    mkdir -p "$DEPS/src"
    if [[ ! -f "$src/Makefile" ]]; then
        say "Downloading MuPDF $MUPDF_VERSION"
        if curl -fL --retry 3 -o "$DEPS/src/mupdf.tar.gz" "https://mupdf.com/downloads/archive/mupdf-$MUPDF_VERSION-source.tar.gz"; then
            tar -xzf "$DEPS/src/mupdf.tar.gz" -C "$DEPS/src" && rm -f "$DEPS/src/mupdf.tar.gz"
        else
            say "mupdf.com not reachable, using GitHub"
            rm -rf "$src"
            git clone -q --depth 1 --branch "$MUPDF_VERSION" https://github.com/ArtifexSoftware/mupdf.git "$src"
            git -C "$src" submodule update --init --depth 1 thirdparty/jbig2dec thirdparty/mujs thirdparty/freetype \
                thirdparty/gumbo-parser thirdparty/harfbuzz thirdparty/libjpeg thirdparty/lcms2 thirdparty/openjpeg \
                thirdparty/zlib thirdparty/extract thirdparty/brotli thirdparty/cmark-gfm
        fi
    fi
    say "Building MuPDF $MUPDF_VERSION (a few minutes, once)"
    make -C "$src" -j"$JOBS" build=release HAVE_X11=no HAVE_GLUT=no HAVE_CURL=no XCFLAGS=-fPIC \
        prefix="$MUPDF_DIR" install-libs >"$LOGS/mupdf-build.log" 2>&1 \
        || { tail -30 "$LOGS/mupdf-build.log"; die "MuPDF build failed, log: logs/mupdf-build.log"; }
}

cmd_setup() {
    mkdir -p "$DEPS"
    if command -v apt-get >/dev/null; then setup_packages; else say "No apt-get: install a C++ compiler, CMake, Ninja, git and Python 3 yourself"; fi
    if [[ "$QT_DIR" == "$DEPS/qt/"* ]]; then setup_qt; else say "Using Qt from QT_DIR=$QT_DIR"; fi
    setup_mupdf
    say "Setup done. Next: ./dev.sh build"
}

need_deps() {
    [[ -f "$MUPDF_DIR/lib/libmupdf.a" ]] || die "MuPDF missing - run ./dev.sh setup"
    [[ -d "$QT_DIR/lib/cmake/Qt6" || -d "$QT_DIR/lib/x86_64-linux-gnu/cmake/Qt6" ]] || die "Qt 6 not found at $QT_DIR - run ./dev.sh setup"
}

# ---------------------------------------------------------------- build / test / run
cmd_build() {
    need_deps
    say "Building Linux version in $BUILD"
    {
        cmake -S "$ROOT" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE="${BUILD_TYPE:-Release}" \
            -DCMAKE_PREFIX_PATH="$QT_DIR" -DMUPDF_ROOT="$MUPDF_DIR" -DPDFABLE_TESTS=ON -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
        cmake --build "$BUILD" -j"$JOBS"
    } > >(tee "$LOGS/linux-build.log") 2>&1
}

qt_env() {
    export LD_LIBRARY_PATH="$QT_DIR/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
    export QT_PLUGIN_PATH="$QT_DIR/plugins"
}

cmd_test() {
    [[ -x "$BUILD/pdfable_tests" ]] || cmd_build
    qt_env
    local out="$BUILD/test-out"
    rm -rf "$out"
    say "Running the self-test (screenshots in $out)"
    QT_QPA_PLATFORM=offscreen "$BUILD/pdfable_tests" "$ROOT/tests/sample.pdf" "$out" 2>&1 | tee "$LOGS/test.log"
}

cmd_run() {
    [[ -x "$BUILD/PDFable" ]] || cmd_build
    qt_env
    exec "$BUILD/PDFable" "$@"
}

# ---------------------------------------------------------------- AppImage
fetch_tool() {
    local name="$1" url="$2"
    mkdir -p "$DEPS/tools"
    if [[ ! -x "$DEPS/tools/$name" ]]; then
        say "Downloading $name"
        curl -fL --retry 3 -o "$DEPS/tools/$name" "$url"
        chmod +x "$DEPS/tools/$name"
    fi
}

cmd_appimage() {
    cmd_build
    local ver appdir
    ver="$(version)"
    appdir="$BUILD/AppDir"
    fetch_tool linuxdeploy-x86_64.AppImage \
        https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
    fetch_tool linuxdeploy-plugin-qt-x86_64.AppImage \
        https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage
    say "Packaging AppImage"
    rm -rf "$appdir"
    DESTDIR="$appdir" cmake --install "$BUILD" --prefix /usr >/dev/null
    mkdir -p "$ROOT/dist"
    local qmake="$QT_DIR/bin/qmake"
    [[ -x "$qmake" ]] || qmake="$(command -v qmake6)"
    (
        cd "$BUILD"
        export APPIMAGE_EXTRACT_AND_RUN=1 QMAKE="$qmake" LDAI_OUTPUT="PDFable-$ver-x86_64.AppImage" OUTPUT="PDFable-$ver-x86_64.AppImage"
        export LD_LIBRARY_PATH="$QT_DIR/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
        "$DEPS/tools/linuxdeploy-x86_64.AppImage" --appdir "$appdir" \
            --desktop-file "$appdir/usr/share/applications/pdfable.desktop" \
            --icon-file "$appdir/usr/share/icons/hicolor/256x256/apps/pdfable.png" \
            --plugin qt --output appimage >"$LOGS/appimage.log" 2>&1 \
            || { tail -30 "$LOGS/appimage.log"; die "AppImage packaging failed, log: logs/appimage.log"; }
        mv -f "PDFable-$ver-x86_64.AppImage" "$ROOT/dist/"
    )
    say "Done: dist/PDFable-$ver-x86_64.AppImage"
}

# ---------------------------------------------------------------- Windows (from WSL)
cmd_windows() {
    is_wsl || die "the Windows build runs from WSL (or double-click build.bat on Windows)"
    command -v cmd.exe >/dev/null || die "cmd.exe not reachable - WSL interop is switched off"
    local src="$ROOT"
    if [[ "$ROOT" != /mnt/[a-z]/* ]]; then
        # Visual Studio cannot build from the Linux file system: mirror the sources to Windows
        local winhome
        winhome="$(wslpath "$(cmd.exe /c 'echo %LOCALAPPDATA%' 2>/dev/null | tr -d '\r')")"
        src="$winhome/PDFable-src"
        say "Copying sources to $src"
        mkdir -p "$src"
        rsync -a --delete --exclude .git --exclude build --exclude deps --exclude dist "$ROOT/" "$src/"
    fi
    say "Building the Windows version with Visual Studio (log: build_log.txt)"
    local rc=0
    (cd "$src" && cmd.exe /c build.bat nopause) || rc=$?
    tail -5 "$src/build_log.txt" | tr -d '\r'
    [[ $rc -eq 0 ]] || die "Windows build failed (exit $rc), see $src/build_log.txt"
    if [[ "$src" != "$ROOT" ]]; then mkdir -p "$ROOT/dist" && cp -f "$src"/dist/*.zip "$ROOT/dist/"; fi
    say "Done: dist/PDFable-$(version)-windows-x64.zip"
}

# every run is also written to logs/dev-<command>.log
case "${1:-help}" in setup|build|test|appimage|windows|all|packages) exec > >(tee "$LOGS/dev-$1.log") 2>&1 ;; esac

case "${1:-help}" in
    packages) setup_packages ;;
    setup) cmd_setup ;;
    build) cmd_build ;;
    test) cmd_test ;;
    run) shift; cmd_run "$@" ;;
    creator) cmd_run --creator ;;
    appimage) cmd_appimage ;;
    windows) cmd_windows ;;
    all) cmd_build; cmd_test; cmd_appimage; if is_wsl; then cmd_windows; fi ;;
    clean) rm -rf "$BUILD"; say "Removed $BUILD" ;;
    *) sed -n '2,20p' "$0" | sed 's/^# \{0,1\}//' ;;
esac
