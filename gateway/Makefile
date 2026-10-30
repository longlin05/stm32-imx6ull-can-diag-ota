CC ?= gcc
CFLAGS ?= -O2 -g -Wall -Wextra -Wpedantic -std=c11
CPPFLAGS ?= -Iinclude

TARGET := build/can_recv
SRCS := src/can_recv.c src/can.c
HDRS := include/can.h include/can_protocol.h

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SRCS) $(HDRS) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $(SRCS)

build:
	mkdir -p build

clean:
	rm -rf build
