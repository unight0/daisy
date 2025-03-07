
CC=gcc
DEBUGFLAGS=-fsanitize=address -g
CCFLAGS=-Wall -Wextra -Wpedantic

kern: kern.c
	$(CC) $? $(CCFLAGS) $(DEBUGFLAGS) -o $@
