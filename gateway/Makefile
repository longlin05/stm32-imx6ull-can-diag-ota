# Gateway 构建脚本
# 本机(x86)编译:  make
# 交叉编译到 imx6ull(NXP SDK 工具链，先 source 环境脚本):  make

CC      ?= gcc
CFLAGS  ?= -Wall -Wextra -O2
CFLAGS  += -Iinclude

# SQLite 可选：有 pkg-config 且装了 sqlite3 才启用
SQLITE_CFLAGS ?= $(shell pkg-config --cflags sqlite3 2>/dev/null)
SQLITE_LIBS   ?= $(shell pkg-config --libs sqlite3 2>/dev/null)
ifneq ($(strip $(SQLITE_LIBS)),)
CFLAGS += -DGW_WITH_SQLITE $(SQLITE_CFLAGS)
endif

GW     = gw
MOCK   = mock_ecu
SELF   = selftest
UDS    = uds_cli

# isotp.c 是平台无关核心(STM32 复用)；can.c 是 Linux 适配层(STM32 侧用 HAL 替代)。
GW_OBJ   = src/main.o src/isotp.o src/can.o
MOCK_OBJ = test/mock_ecu.o src/isotp.o src/can.o
SELF_OBJ = test/selftest.o src/isotp.o
UDS_OBJ  = src/uds_cli.o src/uds_client.o src/gw_log.o src/isotp.o src/can.o

all: $(GW) $(MOCK) $(SELF) $(UDS)

$(GW): $(GW_OBJ)
	$(CC) $(CFLAGS) -o $@ $(GW_OBJ)

$(MOCK): $(MOCK_OBJ)
	$(CC) $(CFLAGS) -o $@ $(MOCK_OBJ)

$(SELF): $(SELF_OBJ)
	$(CC) $(CFLAGS) -o $@ $(SELF_OBJ)

$(UDS): $(UDS_OBJ)
	$(CC) $(CFLAGS) -o $@ $(UDS_OBJ) $(SQLITE_LIBS)

test: $(SELF)
	./$(SELF)

src/%.o: src/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

test/%.o: test/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(GW) $(MOCK) $(SELF) $(UDS) src/*.o test/*.o

.PHONY: all clean test
