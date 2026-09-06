#!/usr/bin/env bash
# IceNvim 收尾脚本:放置编译好的 fzf-native 产物
# 用法: bash tools/nvim-ice/finish.sh
set -euo pipefail

FZF_SRC="$(dirname "$0")/libfzf.so"
FZF_DST="$HOME/.local/share/nvim/lazy/telescope-fzf-native.nvim/build/libfzf.so"

echo "==> 放置 telescope-fzf-native 编译产物"
if [ ! -f "$FZF_SRC" ]; then
  echo "未找到 $FZF_SRC,跳过(可自行在 nvim 内执行 :Lazy build telescope-fzf-native.nvim)"
  exit 0
fi
mkdir -p "$(dirname "$FZF_DST")"
cp "$FZF_SRC" "$FZF_DST"
echo "已复制到 $FZF_DST"

echo "==> 验证"
if ldd "$FZF_DST" >/dev/null 2>&1; then
  echo "libfzf.so 可加载"
else
  echo "警告: libfzf.so 动态链接异常"
fi
echo "完成。重新打开 nvim 即可使用 fzf 加速。"
