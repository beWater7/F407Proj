# 供 build 脚本 source：设置野火 CMSIS-DAP 所需的 pyOCD 环境
# 用法: . "$PROJ_ROOT/tools/env-pyocd.sh"
# 或:  TOOLS_DIR=... . "$TOOLS_DIR/env-pyocd.sh"
if [ -z "${TOOLS_DIR:-}" ]; then
  if [ -n "${BASH_SOURCE[0]:-}" ]; then
    TOOLS_DIR="$(CDPATH= cd -- "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
  elif [ -n "${PROJ_ROOT:-}" ]; then
    TOOLS_DIR="${PROJ_ROOT}/tools"
  else
    echo "env-pyocd.sh: set TOOLS_DIR or PROJ_ROOT before sourcing" >&2
    return 1 2>/dev/null || exit 1
  fi
fi
export PYOCD_USB_BACKEND="${PYOCD_USB_BACKEND:-hidapiusb}"
export PYTHONPATH="${TOOLS_DIR}/hid_hidraw_shim${PYTHONPATH:+:$PYTHONPATH}"
