CC = gcc
CFLAGS = -Wall -Wextra -pthread -Icommon/include
LDFLAGS = -pthread

# Phony targets
.PHONY: all clean

# Default target
all: client_app server_app

# Server executable
server_app: server/server.o server/thread_logic.o common/src/common.o
	$(CC) $(CFLAGS) -o server_app $^ $(LDFLAGS)

# Client executable
client_app: client/client.o common/src/common.o
	$(CC) $(CFLAGS) -o client_app $^ $(LDFLAGS)

# Object files
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Clean rule
clean:
	rm -f server_app client_app server/*.o client/*.o common/src/*.o
