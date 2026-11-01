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
	src/stack.c \
	src/utility.c \
	src/types.c \
	src/snapshot.c \
	src/hash.c \
	src/walk.c \
	src/scan.c \
	src/create.c \
	src/list.c \
	src/diff.c \
	src/verify.c

TESTS := tests/run.sh

OBJECTS := $(SOURCES:%.c=$(BUILD_DIR)/%.o)
DEPENDENCIES := $(OBJECTS:.o=.d)

.PHONY: all clean run test valgrind install uninstall

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(LDFLAGS) $(OBJECTS) $(LDLIBS) -o $@

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

# Fault injection is compile-time libc redirection, not LD_PRELOAD or a GNU
# linker extension, so the error-path tests stay usable with POSIX toolchains.
FAULT_OBJECTS := $(filter-out $(BUILD_DIR)/src/main.o $(BUILD_DIR)/src/create.o \
	$(BUILD_DIR)/src/snapshot.o,$(OBJECTS)) \
	$(BUILD_DIR)/tests/create.o $(BUILD_DIR)/tests/snapshot.o $(BUILD_DIR)/tests/faults.o
DEPENDENCIES += $(BUILD_DIR)/tests/create.d $(BUILD_DIR)/tests/snapshot.d \
	$(BUILD_DIR)/tests/faults.d

$(BUILD_DIR)/tests/create.o: src/create.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -Dfsync=test_fsync -Drename=test_rename \
		-Dfclose=test_fclose -c $< -o $@

$(BUILD_DIR)/tests/snapshot.o: src/snapshot.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -Dfstat=test_fstat -Dfclose=test_fclose -c $< -o $@

$(BUILD_DIR)/tests/faults: $(FAULT_OBJECTS)
	$(CC) $(LDFLAGS) $(FAULT_OBJECTS) $(LDLIBS) -o $@

run: $(TARGET)
	./$(TARGET)

# Black-box: the suite drives the built binary, so it needs nothing but sh.
test: $(TARGET) $(BUILD_DIR)/tests/faults
	@FSNAP='$(CURDIR)/$(TARGET)' sh $(TESTS)
	@$(BUILD_DIR)/tests/faults

# The same suite with every invocation under memcheck. -q keeps stderr clean,
# which matters because several tests compare it; a distinct exit code turns any
# finding into a failed test rather than a note nobody reads.
MEMCHECK := valgrind -q --error-exitcode=99 --leak-check=full \
	--errors-for-leak-kinds=all --track-origins=yes

valgrind: $(TARGET) $(BUILD_DIR)/tests/faults
	@command -v valgrind >/dev/null 2>&1 || { \
		echo 'valgrind is not installed' >&2; exit 2; }
	@FSNAP='$(CURDIR)/$(TARGET)' VALGRIND='$(MEMCHECK)' sh $(TESTS)
	@$(MEMCHECK) $(BUILD_DIR)/tests/faults

install: $(TARGET)
	$(INSTALL) -d $(DESTDIR)$(BINDIR)
	$(INSTALL) -m 755 $(TARGET) $(DESTDIR)$(BINDIR)/$(NAME)

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(NAME)

clean:
	rm -rf $(BUILD_DIR)

-include $(DEPENDENCIES)
