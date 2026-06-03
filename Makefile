CC = gcc
CFLAGS = -D_GNU_SOURCE -Wall -Wextra -Wvla -pedantic -g -Iinclude
LDFLAGS = -lpthread
SRCS = $(shell find src -name '*.c')
OBJS = $(SRCS:.c=.o)

maester: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f maester $(OBJS)

.PHONY: clean
