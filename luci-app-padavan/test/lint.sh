#!/bin/sh
# luci-app-padavan 静态检查：
#   1) 所有 .lua 用 luac -p 做语法检查
#   2) 所有源码文件强制 LF 行尾（CRLF 会导致 BusyBox ash / procd 启动失败）
#
# 用法： make -C luci-app-padavan lint   或   ./test/lint.sh
set -eu

DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$DIR/.." && pwd)

rc=0
CR=$(printf '\r')

# 收集需要检查的文件（luasrc / root 下的 .lua/.htm/.json/.sh）
FILES=$(find "$ROOT/luasrc" "$ROOT/root" "$DIR" \
	-type f \( -name '*.lua' -o -name '*.htm' -o -name '*.json' -o -name '*.sh' \) 2>/dev/null | sort)

for f in $FILES; do
	rel=${f#"$ROOT"/}

	# LF 行尾检查
	if LC_ALL=C grep -q "$CR" "$f"; then
		echo "CRLF 行尾: $rel" >&2
		rc=1
	fi

	# Lua 语法检查
	case "$f" in
		*.lua)
			LUAC=""
			for c in luac5.1 luac5.3 luac5.4 luac; do
				if command -v "$c" >/dev/null 2>&1; then
					LUAC=$c
					break
				fi
			done
			if [ -n "$LUAC" ]; then
				if ! "$LUAC" -p "$f"; then
					echo "语法错误: $rel" >&2
					rc=1
				fi
			fi
			;;
	esac
done

if [ "$rc" -eq 0 ]; then
	echo "lint: 通过（$(printf '%s\n' "$FILES" | wc -l | tr -d ' ') 个文件，无 CRLF / 语法错误）"
fi

exit $rc
