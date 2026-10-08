#!/usr/bin/env python3
"""fake_uci.py - 模拟 uci 命令的测试桩（仅供 libnvram UCI CLI 后端单测）

用法：NVRAM_UCI_BIN=/path/to/fake_uci.py UCI_CONFIG_DIR=<cfgdir> ./test_nvram
支持命令：
  uci get <cfg>.<sec>.<opt>      -> 打印值（无引号）；不存在 exit 1
  uci set <cfg>.<sec>.<opt>=<v>  -> 写配置（立即落盘，模拟 commit）
  uci delete <cfg>.<sec>.<opt>   -> 删除 option；不存在 exit 1
  uci delete <cfg>               -> 删除整个配置包
  uci commit <cfg>               -> 落盘（本桩 set 即落盘，no-op）
  uci show <cfg>                 -> 打印 padavan.<sec>.<opt>='<v>' 每行

配置文件格式（简化 UCI 语法）：
  config 'type' 'secname'
  option 'key' 'value'
  option 'key2' ''
"""
import os
import sys

CONFIG_DIR = os.environ.get("UCI_CONFIG_DIR", "/etc/config")
CONFIG_NAME = "padavan"
CONFIG_FILE = os.path.join(CONFIG_DIR, CONFIG_NAME)


def load_config():
    """返回 {sec: {key: val}}，保持 sec/key 顺序用列表。"""
    secs = []  # [(secname, {key: val})]
    cur = None
    if not os.path.isfile(CONFIG_FILE):
        return secs
    with open(CONFIG_FILE, encoding="utf-8") as f:
        for raw in f:
            line = raw.strip()
            if not line or line.startswith("#"):
                continue
            if line.startswith("config"):
                parts = line.split()
                name = parts[2].strip("'") if len(parts) > 2 else None
                cur = (name, {})
                secs.append(cur)
            elif line.startswith("option") and cur is not None:
                parts = line.split(None, 2)
                if len(parts) >= 2:
                    key = parts[1].strip("'")
                    val = parts[2].strip("'") if len(parts) > 2 else ""
                    cur[1][key] = val
    return secs


def save_config(secs):
    os.makedirs(CONFIG_DIR, exist_ok=True)
    with open(CONFIG_FILE, "w", encoding="utf-8") as f:
        for name, opts in secs:
            f.write("config 'nvram' '{}'\n".format(name))
            for k, v in opts.items():
                f.write("option '{}' '{}'\n".format(k, v))


def find(secs, sec):
    for name, opts in secs:
        if name == sec:
            return opts
    return None


def main():
    args = sys.argv[1:]
    if not args:
        sys.exit(1)
    cmd = args[0]
    if cmd == "commit":
        return 0
    if len(args) < 2:
        sys.exit(1)
    target = args[1]

    # set 命令形如 padavan.sec.opt=value，需先剥离 value 再解析路径
    if cmd == "set":
        if "=" not in target:
            sys.exit(1)
        path, value = target.split("=", 1)
        value = value.strip("'")
    else:
        path = target
        value = None

    parts = path.split(".")
    if parts[0] != CONFIG_NAME:
        sys.exit(1)

    secs = load_config()

    if cmd == "show":
        for name, opts in secs:
            for k, v in opts.items():
                print("{}.{}.{}='{}'".format(CONFIG_NAME, name, k, v))
        return 0

    if cmd == "delete" and len(parts) == 1:  # delete <cfg>（整个包）
        if os.path.isfile(CONFIG_FILE):
            os.remove(CONFIG_FILE)
        return 0

    if len(parts) < 2:
        sys.exit(1)

    sec, opt = parts[1], parts[2]
    opts = find(secs, sec)

    if cmd == "get":
        if opts is None or opt not in opts:
            sys.exit(1)
        print(opts[opt])
        return 0

    if cmd == "set":
        if opts is None:
            opts = {}
            secs.append((sec, opts))
        opts[opt] = value
        save_config(secs)
        return 0

    if cmd == "delete":
        if opts is None or opt not in opts:
            sys.exit(1)
        del opts[opt]
        save_config(secs)
        return 0

    sys.exit(1)


if __name__ == "__main__":
    sys.exit(main())
