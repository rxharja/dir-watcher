CC := gcc
CPPFLAGS := -Iinclude -MMD -MP -D_GNU_SOURCE
CFLAGS := -std=gnu17 -Wall -Wextra -g 

SRCS := $(wildcard src/*.c)
OBJS := $(SRCS:src/%.c=build/%.o)
DEPS := $(OBJS:.o=.d)

VENDOR_DIR := vendor
VENDOR_CFLAGS :=

dir-watcher: $(OBJS)
	$(CC) $(CFLAGS) $^ -o $@

build/%.o: src/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

clean:
	rm -rf build dir-watcher

.PHONY: clean

-include $(DEPS)
