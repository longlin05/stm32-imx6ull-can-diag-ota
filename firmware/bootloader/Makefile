CC      ?= gcc
CFLAGS  ?= -std=c99 -Wall -Wextra -O2
TARGET   = test_boot_core
SRC      = boot_crc32.c boot_image.c boot_meta.c boot_policy.c test/test_boot_core.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -I. -o $@ $(SRC)

test: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET) $(TARGET).exe

.PHONY: all test clean
