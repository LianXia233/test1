#!/bin/sh
# luci-app-padavan 本地测试入口（纯 Lua 单测 + 语法/行尾 lint）
#
# 用法： make -C luci-app-padavan test   或   ./test/run.sh
# 依赖： Lua 5.1（或 5.3/5.4）解释器；luac 可选（缺失时跳过语法检查）
set -eu

DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$DIR/.." && pwd)

# 先做语法 / 行尾检查（luac 缺失时 lint.sh 内部自行跳过）
"$DIR/lint.sh"

LUA=""
for c in lua5.1 lua5.3 lua5.4 lua luajit; do
	if command -v "$c" >/dev/null 2>&1; then
		LUA=$c
		break
	fi
done

if [ -z "$LUA" ]; then
	echo "错误：未找到 Lua 解释器（尝试 lua5.1 / lua5.3 / lua / luajit）" >&2
	exit 1
fi

PADAVAN_LUCI_SRC="$ROOT/luasrc" "$LUA" "$DIR/test_padavan.lua"
