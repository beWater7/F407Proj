#!/bin/sh
# APP menuconfig / sync autoconf.h
# Prefer classic Linux-style frontends (kconfig-frontends), fall back to Kconfiglib.
set -e
cd "$(dirname "$0")/.."
APP_ROOT="$PWD"
VENDORED_KCFG="$APP_ROOT/tools/kconfig-frontends"

export PATH="$VENDORED_KCFG/bin:$HOME/.local/bin:$PATH"
# parser .so: vendored first, then ~/.local/lib
export LD_LIBRARY_PATH="$VENDORED_KCFG/lib:$HOME/.local/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export KCONFIG_CONFIG="${KCONFIG_CONFIG:-$PWD/.config}"

CMD="${1:-menuconfig}"

have() { command -v "$1" >/dev/null 2>&1; }

# Classic Linux look (kconfig-mconf / nconf)
run_classic_menuconfig() {
  if have kconfig-mconf; then
    echo "==> using kconfig-mconf (Linux-style)"
    kconfig-mconf Kconfig
    return 0
  fi
  if have kconfig-nconf; then
    echo "==> using kconfig-nconf (Linux-style)"
    kconfig-nconf Kconfig
    return 0
  fi
  return 1
}

run_classic_guiconfig() {
  if have kconfig-qconf; then
    echo "==> using kconfig-qconf"
    kconfig-qconf Kconfig
    return 0
  fi
  if have kconfig-gconf; then
    echo "==> using kconfig-gconf"
    kconfig-gconf Kconfig
    return 0
  fi
  return 1
}

run_kconfiglib() {
  tool="$1"
  if ! have "$tool"; then
    echo "error: '$tool' not found (pip install kconfiglib)" >&2
    echo "hint: sudo apt install kconfig-frontends-nox" >&2
    exit 1
  fi
  echo "==> using Kconfiglib $tool (fallback)"
  "$tool"
}

case "$CMD" in
  menuconfig)
    run_classic_menuconfig || run_kconfiglib menuconfig
    ;;
  guiconfig|xconfig)
    run_classic_guiconfig || run_kconfiglib guiconfig
    ;;
  nconfig)
    if have kconfig-nconf; then
      echo "==> using kconfig-nconf"
      kconfig-nconf Kconfig
    else
      run_classic_menuconfig || run_kconfiglib menuconfig
    fi
    ;;
  oldconfig|olddefconfig|allnoconfig|allyesconfig)
    if have kconfig-conf; then
      case "$CMD" in
        oldconfig) kconfig-conf --oldconfig Kconfig ;;
        olddefconfig) kconfig-conf --olddefconfig Kconfig ;;
        allnoconfig) kconfig-conf --allnoconfig Kconfig ;;
        allyesconfig) kconfig-conf --allyesconfig Kconfig ;;
      esac
    elif have "$CMD"; then
      "$CMD"
    else
      echo "error: neither kconfig-conf nor $CMD found" >&2
      exit 1
    fi
    ;;
  defconfig)
    if [ -f defconfig ]; then
      cp -f defconfig "$KCONFIG_CONFIG"
      if have kconfig-conf; then
        kconfig-conf --olddefconfig Kconfig
      elif have defconfig; then
        defconfig defconfig
      fi
    else
      echo "error: no defconfig" >&2
      exit 1
    fi
    ;;
  sync|"")
    ;;
  -h|--help|help)
    cat <<EOF
Usage: $0 [menuconfig|nconfig|guiconfig|olddefconfig|allnoconfig|sync]

  menuconfig   TUI (prefer kconfig-mconf = Linux look)
  nconfig      ncurses frontend if available
  guiconfig    GUI (prefer kconfig-qconf)
  sync         only regenerate include/generated/autoconf.h from .config

Classic UI is vendored under tools/kconfig-frontends/ (Linux-style).
Optional system package: sudo apt install kconfig-frontends-nox
EOF
    exit 0
    ;;
  *)
    echo "unknown: $CMD" >&2
    exit 1
    ;;
esac

mkdir -p include/generated
if [ ! -f .config ]; then
  echo "==> no .config, using allnoconfig (all optional features off)"
  if have kconfig-conf; then
    kconfig-conf --allnoconfig Kconfig
  elif have allnoconfig; then
    allnoconfig
  else
    echo "error: cannot create .config" >&2
    exit 1
  fi
fi

# Prefer Kconfiglib genconfig for autoconf.h (Makefile uses the same)
if have genconfig; then
  genconfig --header-path include/generated/autoconf.h
else
  echo "warn: genconfig missing; pip install --user kconfiglib" >&2
fi
echo "==> wrote $PWD/include/generated/autoconf.h"
grep '^CONFIG_APP_' .config 2>/dev/null || grep 'CONFIG_APP_' include/generated/autoconf.h || true
