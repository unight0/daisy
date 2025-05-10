.PHONY: install

PWD=$(shell pwd)
# Set this to your installation prefix
# E.g. .local
PREFIX=$(PWD)
CC=gcc
DEBUGFLAGS=-fsanitize=address -g
INCL_PATH=$(PREFIX)/include/
BIN_PATH=$(PREFIX)/bin/
CCFLAGS=-Wall -Wextra -Werror
LDFLAGS=#-lreadline
# These are the optional features of the kernel
# Modify them to suit your needs
# Documentation about each feature will be written
# in future updates.
FEATURES=-DFT_DOCS -DINCLUDE_PATH='"$(INCL_PATH)"'

kern: kern.c
	$(CC) $? $(CCFLAGS) $(LDFLAGS) $(FEATURES) -o $@

debug: kern.c
	$(CC) $? $(CCFLAGS) $(DEBUGFLAGS) $(LDFLAGS) $(FEATURES) -o $@

install: kern
	[ -d $(BIN_PATH) ] || mkdir $(BIN_PATH)
	[ -d $(INCL_PATH) ] || mkdir $(INCL_PATH)
	mv $? $(BIN_PATH)
	[ $(PWD)/include/ == $(INCL_PATH) ] || cp $(PWD)/include/* $(INCL_PATH)
