#!/usr/bin/env bash
# 薄入口：实际脚本在 tools/serial/
exec "$(cd "$(dirname "$0")" && pwd)/tools/serial/serialTerm.sh" "$@"
