# rc-ng 服务层骨架 Makefile
# 依赖: ../libnvram (M2 产物), 需先 make -C ../libnvram
CC      ?= gcc
CFLAGS  ?= -Wall -Wextra -O2 -g
CFLAGS  += -Iinclude -I../libnvram/include
LDFLAGS += ../libnvram/libnvram.a

SRC = src/service_registry.c src/notify_bus.c src/main.c
OBJ = $(SRC:.c=.o)

all: rc-ng libpadavan_rc.a

rc-ng: $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(LDFLAGS)

libpadavan_rc.a: src/service_registry.o src/notify_bus.o
	$(AR) rcs $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

test: test/test_rc.c src/service_registry.c
	$(CC) $(CFLAGS) -o test_rc test/test_rc.c src/service_registry.c $(LDFLAGS)
	./test_rc

clean:
	rm -f $(OBJ) rc-ng libpadavan_rc.a test_rc

.PHONY: all test clean
