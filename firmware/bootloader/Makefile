CC      ?= gcc
CFLAGS  ?= -std=c99 -Wall -Wextra -O2
CORE     = test_boot_core
STORE    = test_boot_store
CORE_SRC = boot_crc32.c boot_image.c boot_meta.c boot_policy.c test/test_boot_core.c
STORE_SRC= boot_crc32.c boot_meta.c boot_meta_store.c test/test_boot_store.c

all: $(CORE) $(STORE)

$(CORE): $(CORE_SRC)
	$(CC) $(CFLAGS) -I. -o $@ $(CORE_SRC)

$(STORE): $(STORE_SRC)
	$(CC) $(CFLAGS) -I. -o $@ $(STORE_SRC)

test: all
	./$(CORE)
	./$(STORE)

clean:
	rm -f $(CORE) $(STORE) $(CORE).exe $(STORE).exe

.PHONY: all test clean
