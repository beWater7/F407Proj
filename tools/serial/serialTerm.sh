#!/usr/bin/env bash
# 启动串口终端：默认日志 ~/gitProj/log，并确保免 sudo 访问串口（dialout 组）

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
LOG_DIR="${HOME}/gitProj/log"
mkdir -p "$LOG_DIR"
export SERIAL_LOG_DIR="$LOG_DIR"

# 当前会话是否已有 dialout 权限
has_dialout_now() {
  id -nG 2>/dev/null | tr ' ' '\n' | grep -qx dialout
}

# /etc/group 中是否已登记（可能尚未生效到当前会话）
user_in_dialout() {
  id -nG "${USER:-$(id -un)}" 2>/dev/null | tr ' ' '\n' | grep -qx dialout
}

# 一次性把用户加入 dialout，之后无需每次 sudo
ensure_dialout_group() {
  if has_dialout_now || user_in_dialout; then
    return 0
  fi
  if ! getent group dialout >/dev/null 2>&1; then
    echo "[serialTerm] 系统无 dialout 组，跳过组权限配置"
    return 1
  fi
  echo "[serialTerm] 用户不在 dialout 组，无法免 sudo 打开串口"
  echo "[serialTerm] 将执行一次性配置: sudo usermod -aG dialout ${USER:-$(id -un)}"
  if sudo usermod -aG dialout "${USER:-$(id -un)}"; then
    echo "[serialTerm] 已加入 dialout。本次用 sg 立即生效；重新登录后永久生效。"
    return 0
  fi
  echo "[serialTerm] 加入 dialout 失败，将尝试临时授权当前设备"
  return 1
}

# 对已插入的 USB 串口做当前会话临时授权（插拔后可能需再跑一次）
grant_current_devices() {
  local dev granted=0
  for dev in /dev/ttyUSB* /dev/ttyACM*; do
    [ -e "$dev" ] || continue
    if [ -r "$dev" ] && [ -w "$dev" ]; then
      continue
    fi
    echo "[serialTerm] 临时授权访问 $dev ..."
    if sudo setfacl -m "u:${USER:-$(id -un)}:rw" "$dev" 2>/dev/null \
      || sudo chmod 666 "$dev" 2>/dev/null; then
      granted=1
    else
      echo "[serialTerm] 授权失败: $dev"
    fi
  done
  return 0
}

ensure_dialout_group
grant_current_devices

cd "$SCRIPT_DIR" || exit 1

if has_dialout_now; then
  exec python3 "$SCRIPT_DIR/serialTerm.py" "$@"
fi

# 已在组内但当前 shell 未带上该组：用 sg 无需重新登录
if user_in_dialout && command -v sg >/dev/null 2>&1; then
  exec sg dialout -c "cd \"$SCRIPT_DIR\" && SERIAL_LOG_DIR=\"$LOG_DIR\" exec python3 \"$SCRIPT_DIR/serialTerm.py\" $(printf '%q ' "$@")"
fi

exec python3 "$SCRIPT_DIR/serialTerm.py" "$@"
