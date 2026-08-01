CC := cc

CPPFLAGS := -Iinclude -MMD -MP
CFLAGS := -std=c11 -Wall -Wextra -Wpedantic -Wconversion
LDFLAGS :=
LDLIBS :=

BUILD_DIR := build
TARGET := $(BUILD_DIR)/fsnap

SOURCES := \
	src/main.c \
	src/utility.c \
	src/walk.c \
	src/scan.c \
	src/create.c

OBJECTS := $(SOURCES:%.c=$(BUILD_DIR)/%.o)
DEPENDENCIES := $(OBJECTS:.o=.d)

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(OBJECTS) | $(BUILD_DIR)
	$(CC) $(LDFLAGS) $(OBJECTS) $(LDLIBS) -o $@

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR):
	mkdir -p $@

run: $(TARGET)
	./$(TARGET)

clean:
	rm -rf $(BUILD_DIR)

-include $(DEPENDENCIES)
