# ===== Makefile: build only the RPSD server =====
CC      := gcc
CFLAGS  := -g -Wall -Wextra -std=c11 -fsanitize=address,undefined \
           -D_POSIX_C_SOURCE=200809L -pthread
LDFLAGS := $(CFLAGS)

# ----------------------------------------------------------------
# Source- and object-file lists
SRCS  := rpsd.c network.c
OBJS  := $(SRCS:.c=.o)

EXEC  := rpsd            # final executable name

# ----------------------------------------------------------------
# Default target
all: $(EXEC)

# Link step
$(EXEC): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^

# Compile step
%.o: %.c network.h
	$(CC) $(CFLAGS) -c $< -o $@

# ----------------------------------------------------------------
# House-keeping
clean:
	rm -f $(OBJS) $(EXEC) core.* *~

.PHONY: all clean
