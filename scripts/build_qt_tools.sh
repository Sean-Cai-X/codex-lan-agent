#!/usr/bin/env bash
# Build optfile / directory_access / git_snapshot using a manually extracted Qt6.
# Requires:
#   - /H/usr/aisrc/qt6/                   (manual Qt6 extraction)
#   - /H/usr/aisrc/toolchain/             (manual gcc + binutils + runtime libs)
#   - g++ from toolchain in PATH (handled below)
# Usage: bash scripts/build_qt_tools.sh [build-dir]

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="${1:-$PROJECT_ROOT/build}"

QT6_ROOT="/H/usr/aisrc/qt6"
TC_ROOT="/H/usr/aisrc/toolchain"

QT6_INC="$QT6_ROOT/usr/include/x86_64-linux-gnu/qt6"
QT6_LIB="$QT6_ROOT/usr/lib/x86_64-linux-gnu"
TC_LIB="$TC_ROOT/usr/lib/x86_64-linux-gnu"
GCC_LIB="$TC_ROOT/usr/lib/gcc/x86_64-linux-gnu/14"

export PATH="$TC_ROOT/usr/bin:$PATH"
export LD_LIBRARY_PATH="$TC_LIB:$GCC_LIB${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

mkdir -p "$BUILD_DIR"

CXXFLAGS=(
  -std=c++17 -O2 -DQT_CORE_LIB
  # C++ standard library headers (toolchain)
  -isystem "$TC_ROOT/usr/include/c++/14"
  -isystem "$TC_ROOT/usr/include/x86_64-linux-gnu/c++/14"
  -isystem "$TC_ROOT/usr/include/c++/14/backward"
  -isystem "$GCC_LIB/include"
  # C library headers (toolchain)
  -isystem "$TC_ROOT/usr/include"
  -isystem "$TC_ROOT/usr/include/x86_64-linux-gnu"
  # Qt6 headers
  -isystem "$QT6_INC"
  -isystem "$QT6_INC/QtCore"
)

LDFLAGS=(
  -L"$QT6_LIB" -L"$TC_LIB" -L"$GCC_LIB"
  -L/usr/lib/x86_64-linux-gnu -L/lib/x86_64-linux-gnu
  -lQt6Core -lpthread
  # Use RPATH (not RUNPATH) so transitive Qt6 deps (glib, icu, etc.) resolve
  # before searching LD_LIBRARY_PATH / /etc/ld.so.conf.
  -Wl,--disable-new-dtags
  -Wl,-rpath,"$QT6_LIB:$TC_LIB"
)

build_tool() {
  local name="$1"; local src="$2"
  echo ">>> Building $name"
  g++ "${CXXFLAGS[@]}" "$PROJECT_ROOT/$src" "${LDFLAGS[@]}" -o "$BUILD_DIR/$name"
  echo "    -> $BUILD_DIR/$name"
}

build_tool optfile            "optfile/main.cpp"
build_tool directory_access   "tools/directory_access/main.cpp"
build_tool git_snapshot       "tools/git_snapshot/main.cpp"

echo
echo "All Qt helper tools built successfully."
echo "No LD_LIBRARY_PATH needed — RPATH is embedded in each binary."
