#!/bin/sh
# 为 app / loader / boot 分别生成 compile_commands.json（bear）
# 两套库互不合并，clangd 按当前文件所在树跳转，避免同名函数串到另一边。
set -e

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BEAR="${BEAR:-bear}"

if ! command -v "$BEAR" >/dev/null 2>&1; then
  echo "ERROR: bear not found. Install: sudo apt install bear" >&2
  exit 1
fi

JOBS="$(nproc 2>/dev/null || echo 4)"

gen_one() {
  dir="$1"
  elf_target="$2"
  echo "==> $dir  ($elf_target)"
  cd "$ROOT/$dir"
  # 只清 .o，不 make -B，避免把 app/.config 重写成 defconfig
  rm -rf build/obj
  "$BEAR" --output compile_commands.json -- make -j"$JOBS" "$elf_target"
  n="$(grep -c '"file"' compile_commands.json 2>/dev/null || echo 0)"
  echo "    wrote $dir/compile_commands.json ($n entries)"
}

gen_one app build/app1.elf
gen_one loader build/loader.elf
gen_one boot build/boot.elf
echo "compdb ok. Reload clangd window if the editor is already open."
