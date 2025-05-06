.PHONY: install

PWD=$(shell pwd)
# Set this to your installation prefix
# E.g. .local
PREFIX=$(PWD)
CC=gcc
DEBUGFLAGS=-fsanitize=address -g
INCL_PATH=$(PREFIX)/include/
BIN_PATH=$(PREFIX)/bin/
CCFLAGS=-Wall -Wextra -Wpedantic -Werror -DINCLUDE_PATH='"$(INCL_PATH)"'
LDFLAGS= #-lreadline

kern: kern.c
	$(CC) $? $(CCFLAGS) $(LDFLAGS) -o $@

debug: kern.c
	$(CC) $? $(CCFLAGS) $(DEBUGFLAGS) $(LDFLAGS) -o $@

install: kern
	[ -d $(BIN_PATH) ] || mkdir $(BIN_PATH)
	[ -d $(INCL_PATH) ] || mkdir $(INCL_PATH)
	mv $? $(BIN_PATH)
	[ $(PWD)/include/ == $(INCL_PATH) ] || cp $(PWD)/include/* $(INCL_PATH)
