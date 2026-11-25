CC            := gcc
CFLAGS        := -std=gnu99 -Wall -Wextra -Wswitch-enum 
BUILD ?= release

ifeq ($(BUILD),release)
	CFLAGS      := $(CFLAGS) -O2 
	BUILD_DIR   := build/release
else
	CFLAGS      := $(CFLAGS) -g -fsanitize=address,undefined
	BUILD_DIR   := build/debug
endif

DEPFLAGS      := -MMD -MP 
CPPFLAGS      := -Iinclude -isystem vendor/tomlc17/src -D_GNU_SOURCE

SRCS          := $(wildcard src/*.c)
OBJS          := $(SRCS:src/%.c=$(BUILD_DIR)/%.o)
DEPS          := $(OBJS:.o=.d)

VENDOR_CFLAGS := -std=c17 -g
VENDOR_SRCS   := $(wildcard vendor/tomlc17/src/*.c)
VENDOR_OBJS   := $(VENDOR_SRCS:vendor/%.c=$(BUILD_DIR)/vendor/%.o)
VENDOR_DEPS   := $(VENDOR_OBJS:.o=.d)

all: dir-watcher

dir-watcher: $(OBJS) $(VENDOR_OBJS)
	$(CC) $(CFLAGS) -o $(BUILD_DIR)/$@ $^
	@ln -s -f $(BUILD_DIR)/$@ $@

$(BUILD_DIR)/%.o: src/%.c 
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(DEPFLAGS) $(CFLAGS) -o $@ -c $<

$(BUILD_DIR)/vendor/%.o: vendor/%.c 
	@mkdir -p $(@D)
	$(CC) $(DEPFLAGS) $(VENDOR_CFLAGS) -o $@ -c $<

# if I ever have to change the tomlc17 header and have its
# updates be caught by make
$(OBJS): $(wildcard vendor/tomlc17/src/*.h)

clean:
	rm -rf build dir-watcher

.PHONY: all clean

-include $(DEPS) $(VENDOR_DEPS)
