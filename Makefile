
CC=gcc
DEBUGFLAGS=-fsanitize=address -g
CCFLAGS=-Wall -Wextra -Wpedantic -Werror
LDFLAGS=-lreadline

kern: kern.c
	$(CC) $? $(CCFLAGS) $(DEBUGFLAGS) $(LDFLAGS) -o $@
