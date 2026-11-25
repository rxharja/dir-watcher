CC            := gcc
CFLAGS_LOCAL  := -std=gnu99 -Wall -Wextra -Wswitch-enum 
CFLAGS_VENDOR := -std=c17 -g
CFLAGS        += $(CFLAGS_LOCAL)
BUILD         ?= release

ifeq ($(BUILD),release)
	CFLAGS        += -O2 
	CFLAGS_VENDOR += -O2 
	BUILD_DIR     := build/release
else
	CFLAGS        += -g -fsanitize=address,undefined
	BUILD_DIR     := build/debug
endif

DEPFLAGS      := -MMD -MP 
CPPFLAGS      := -Iinclude -isystem vendor/tomlc17/src -D_GNU_SOURCE

SRCS          := $(wildcard src/*.c)
OBJS          := $(SRCS:src/%.c=$(BUILD_DIR)/%.o)
DEPS          := $(OBJS:.o=.d)

VENDOR_SRCS   := $(wildcard vendor/tomlc17/src/*.c)
VENDOR_OBJS   := $(VENDOR_SRCS:vendor/%.c=$(BUILD_DIR)/vendor/%.o)
VENDOR_DEPS   := $(VENDOR_OBJS:.o=.d)

PREFIX        ?= /usr/local

all: dir-watcher

dir-watcher: $(OBJS) $(VENDOR_OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $(BUILD_DIR)/$@ $^
	@ln -s -f $(BUILD_DIR)/$@ $@

$(BUILD_DIR)/%.o: src/%.c 
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(DEPFLAGS) $(CFLAGS) -o $@ -c $<

$(BUILD_DIR)/vendor/%.o: vendor/%.c 
	@mkdir -p $(@D)
	$(CC) $(DEPFLAGS) $(CFLAGS_VENDOR) -o $@ -c $<

# if I ever have to change the tomlc17 header and have its
# updates be caught by make
$(OBJS): $(wildcard vendor/tomlc17/src/*.h)

archive:
	@test -n "$(VERSION)" || { echo "VERSION is not set" >&2; exit 1; }
	@mkdir -p build
	@git archive --prefix=dir-watcher-$(VERSION)/ -o dir-watcher-$(VERSION).tar $(VERSION) 
	@git -C vendor/tomlc17 archive --prefix=dir-watcher-$(VERSION)/vendor/tomlc17/ \
		-o $(CURDIR)/build/tomlc17.tar $$(git rev-parse $(VERSION):vendor/tomlc17)
	@tar --concatenate --file=dir-watcher-$(VERSION).tar build/tomlc17.tar
	@rm build/tomlc17.tar
	@gzip -f dir-watcher-$(VERSION).tar

install:
	@install -Dm755 $(BUILD_DIR)/dir-watcher $(DESTDIR)$(PREFIX)/bin/dir-watcher

clean:
	rm -rf build dir-watcher dir-watcher-*.tar*

.PHONY: all clean archive install

-include $(DEPS) $(VENDOR_DEPS)
