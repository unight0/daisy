
CCFLAGS=-fsanitize=address -g


kern: kern.c
	cc $? $(CCFLAGS) -o $@
