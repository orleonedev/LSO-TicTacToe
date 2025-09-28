CC = gcc
CFLAGS = -Wall -Wextra -pthread -Icommon/include
LDFLAGS = -pthread

BUILD_DIR = build

# Phony targets
.PHONY: all clean

# Default target
all: $(BUILD_DIR)/client_app $(BUILD_DIR)/server_app

# Create build directory
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# Server executable
$(BUILD_DIR)/server_app: $(BUILD_DIR)/server.o $(BUILD_DIR)/thread_logic.o $(BUILD_DIR)/datastructures.o $(BUILD_DIR)/utils.o | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Client executable
$(BUILD_DIR)/client_app: $(BUILD_DIR)/client.o $(BUILD_DIR)/datastructures.o $(BUILD_DIR)/utils.o | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Object files
$(BUILD_DIR)/client.o: client/client.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/server.o: server/server.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/thread_logic.o: server/thread_logic.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/datastructures.o: common/src/datastructures.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/utils.o: common/src/utils.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Clean rule
clean:
	rm -rf $(BUILD_DIR)
