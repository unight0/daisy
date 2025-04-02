.PHONY: release

#PREFIX=.local
# TODO: edit later to allow installation
PREFIX=.
CC=gcc
DEBUGFLAGS=-fsanitize=address -g
INCL_PATH="$(PREFIX)/include/"
BIN_PATH=$(PREFIX)/bin/
CCFLAGS=-Wall -Wextra -Wpedantic -Werror -DINCLUDE_PATH='$(INCL_PATH)'
LDFLAGS=-lreadline

kern: kern.c
	$(CC) $? $(CCFLAGS) $(LDFLAGS) -o $@

debug: kern.c
	$(CC) $? $(CCFLAGS) $(DEBUGFLAGS) $(LDFLAGS) -o $@
