CC := cc

# Feature test macros belong to the build, not to individual files: declaring
# them per-source drifted into four different combinations across nine files.
# _POSIX_C_SOURCE covers getline and realpath(path, NULL); _DEFAULT_SOURCE adds
# dirent.d_type, which walk uses as a fast path. SPEC.md section 19 targets
# POSIX, so nothing here asks for _GNU_SOURCE.
FEATURES := -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE

CPPFLAGS := -Iinclude -MMD -MP $(FEATURES)

# -O2 is not only about speed: without optimisation GCC skips the flow
# analysis that most of these warnings are derived from.
WARNINGS := -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wvla \
	-Wstrict-prototypes -Wmissing-prototypes -Wold-style-definition \
	-Wcast-qual -Wwrite-strings -Wdouble-promotion
CFLAGS := -std=c11 $(WARNINGS) -O2 -ggdb
LDFLAGS :=
LDLIBS :=

BUILD_DIR := build
NAME := fsnap
TARGET := $(BUILD_DIR)/$(NAME)

# ?= so a caller can override from the environment or the command line.
# DESTDIR is deliberately not defaulted: it is empty for a real install and set
# by a packager staging into a temporary root.
PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
INSTALL ?= install

SOURCES := \
	src/main.c \
	src/utility.c \
	src/types.c \
	src/hash.c \
	src/walk.c \
	src/scan.c \
	src/create.c \
	src/list.c \
	src/diff.c

OBJECTS := $(SOURCES:%.c=$(BUILD_DIR)/%.o)
DEPENDENCIES := $(OBJECTS:.o=.d)

.PHONY: all clean run install uninstall

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(LDFLAGS) $(OBJECTS) $(LDLIBS) -o $@

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

run: $(TARGET)
	./$(TARGET)

install: $(TARGET)
	$(INSTALL) -d $(DESTDIR)$(BINDIR)
	$(INSTALL) -m 755 $(TARGET) $(DESTDIR)$(BINDIR)/$(NAME)

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(NAME)

clean:
	rm -rf $(BUILD_DIR)

-include $(DEPENDENCIES)
