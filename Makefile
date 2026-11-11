CC := gcc
CFLAGS := -std=gnu99 -Wall -Wextra -g 
DEPFLAGS := -MMD -MP 
CPPFLAGS := -Iinclude -isystem vendor/tomlc17/src -D_GNU_SOURCE

SRCS := $(wildcard src/*.c)
OBJS := $(SRCS:src/%.c=build/%.o)

DEPS := $(OBJS:.o=.d)

VENDOR_CFLAGS := -std=c17 -g
VENDOR_SRCS := $(wildcard vendor/tomlc17/src/*.c)
VENDOR_OBJS := $(VENDOR_SRCS:vendor/%.c=build/vendor/%.o)
VENDOR_DEPS := $(VENDOR_OBJS:.o=.d)

dir-watcher: $(OBJS) $(VENDOR_OBJS)
	$(CC) $(CFLAGS) -o $@ $^

build/%.o: src/%.c 
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(DEPFLAGS) $(CFLAGS) -o $@ -c $<

build/vendor/%.o: vendor/%.c 
	@mkdir -p $(@D)
	$(CC) $(DEPFLAGS) $(VENDOR_CFLAGS) -o $@ -c $<

# if I ever have to change the tomlc17 header and have its
# updates be caught by make
$(OBJS): $(wildcard vendor/tomlc17/src/*.h)

clean:
	rm -rf build dir-watcher

.PHONY: clean

-include $(DEPS) $(VENDOR_DEPS)
