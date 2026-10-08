# libnvram - Padavan nvram 重写版 Makefile
#
# 后端：
#   file    纯文件 KV（默认，零依赖）
#   uci_cli 通过 `uci` 命令操作 /etc/config/padavan（默认一并编译）
#   uci     libuci C API（OpenWrt SDK 环境，make WITH_UCI=1）
#
# 用法：
#   make                    # 编译 file + uci_cli 后端 + 测试程序
#   make WITH_UCI=1         # 追加编译 libuci 后端
#   make test               # 运行 file 后端测试 + uci_cli 后端测试（fake_uci 桩）
#   make clean

CC      ?= gcc
CFLAGS  ?= -O2 -Wall -Wextra -std=gnu99
CPPFLAGS += -Iinclude

WITH_UCI ?= 0
ifeq ($(WITH_UCI),1)
CFLAGS += -DWITH_UCI
LDLIBS += -luci
endif

LIB     = libnvram.a
OBJS    = src/nvram_core.o src/nvram_uci_cli.o src/nvram_uci.o
TEST    = test_nvram

.PHONY: all test clean

all: $(LIB) $(TEST)

$(LIB): $(OBJS)
	ar rcs $@ $^

src/nvram_core.o: src/nvram_core.c include/nvram_linux.h include/nvram_backend.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

src/nvram_uci_cli.o: src/nvram_uci_cli.c include/nvram_linux.h include/nvram_backend.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

src/nvram_uci.o: src/nvram_uci.c include/nvram_linux.h include/nvram_backend.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(TEST): test/test_nvram.c $(LIB)
	$(CC) $(CFLAGS) $(CPPFLAGS) $< $(LIB) $(LDLIBS) -o $@

# file 后端测试 + uci_cli 后端测试（fake_uci 桩 + UCI_CONFIG_DIR 隔离目录）
test: $(TEST)
	@echo "== 后端: file =="
	./$(TEST)
	@echo
	@echo "== 后端: uci_cli (fake_uci 桩) =="
	@chmod +x test/fake_uci.py
	@cfgdir=$$(mktemp -d /tmp/fake_uci_cfgXXXXXX); \
	NVRAM_UCI_BIN=$(CURDIR)/test/fake_uci.py \
	UCI_CONFIG_DIR=$$cfgdir \
	./$(TEST) --backend uci_cli

clean:
	rm -f $(LIB) $(OBJS) $(TEST)
