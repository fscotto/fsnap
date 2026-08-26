CC := cc

CPPFLAGS := -Iinclude -MMD -MP

# -O2 is not only about speed: without optimisation GCC skips the flow
# analysis that most of these warnings are derived from.
WARNINGS := -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wvla \
	-Wstrict-prototypes -Wmissing-prototypes -Wold-style-definition \
	-Wcast-qual -Wwrite-strings -Wdouble-promotion
CFLAGS := -std=c11 $(WARNINGS) -O2 -ggdb
LDFLAGS :=
LDLIBS :=

BUILD_DIR := build
TARGET := $(BUILD_DIR)/fsnap

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

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(LDFLAGS) $(OBJECTS) $(LDLIBS) -o $@

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

run: $(TARGET)
	./$(TARGET)

clean:
	rm -rf $(BUILD_DIR)

-include $(DEPENDENCIES)
