CC := gcc
CPPFLAGS := -Iinclude -MMD -MP -D_GNU_SOURCE
CFLAGS := -std=gnu17 -Wall -Wextra -g 

SRCS := $(wildcard src/*.c)
OBJS := $(SRCS:src/%.c=build/%.o)

VENDOR_DIR := vendor
VENDORS := $(VENDOR_DIR)/*
TOML_DIR := $(VENDOR_DIR)/tomlc17/src
VENDOR_SRCS := $(TOML_DIR)/tomlc17.c
VENDOR_OBJS := $(VENDOR_SRCS:$(VENDOR_DIR)/%.c=build/vendor/%.o)
VENDOR_CFLAGS := -std=c17 -g
CPPFLAGS += -isystem $(TOML_DIR)

DEPS := $(OBJS:.o=.d) $(VENDOR_OBJS:.o=.d)

dir-watcher: $(OBJS) $(VENDOR_OBJS)
	$(CC) $(CFLAGS) $^ -o $@

build/%.o: src/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

build/vendor/%.o: $(VENDOR_DIR)/%.c 
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(VENDOR_CFLAGS) -c $< -o $@

clean:
	rm -rf build dir-watcher

.PHONY: clean

-include $(DEPS
