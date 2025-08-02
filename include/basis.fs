
: bye 0 exit ;
: abort 1 exit ;

: != = not ;

: 1+ 1 + ;
: 1- 1 - ;

: cell 1 cells ;

: , here ! cell reserve ;

: ,b here !b 1 reserve ;

: [ immediate compile-only 0 state !b ;

: ] 1 state !b ;

: ,, [ ' lit dup , , ] , , ;

: ['] immediate compile-only ' ,, ;

: postpone immediate ' ,, ['] , , ;

: if immediate compile-only here cell + 0 ,, postpone 0branch ;

: endif immediate compile-only here swap ! ;

: while immediate compile-only here ;

: do immediate compile-only here cell + 0 ,, postpone 0branch ;

: done immediate compile-only swap ,, postpone branch here swap ! ;

: \ immediate while eb dup 10 != swap 0 != and do done ;
\ This is a one-line-comment!

: ( immediate while eb dup ')' != swap 0 != and do done ;

\ ( A B -- B ) writes cell A to address B preserving B on stack
: !! dup rot swap ! ;

\ ( A B -- B ) writes byte A to addres B preserving B on stack
: !!b dup rot swap !b ;

( This is a
  multiline comment
  Note: multiline comments don't work in REPL. )

\ SUPER-MEGA-ACHTUNG!!
\ { places HERE at the top of the stack, and } uses it!
\ Therefore, the top element on the stack upon executing } should be
\ the HERE left by the {
\ If you fail to comply with this, it produces a hardly-debuggable segfault!
\ Be careful with custom IMMEDIATE words!
\ NOTICE: SAME APPLIES FOR IF .. ENDIF and WHILE .. DO .. DONE!

\ Briefly enter compilation mode, execute the
\ created word, and then delete it
\ ( -- here)
: { interpretation-only here 1 state !b ;
\ We are unreserving memory before we execute its contents,
\ but it is okay because we are only running one thread.
\ Additionally, when (or IF) we implement a multithreading model,
\ It will be designed in such a way that this wouldn't be an issue.
\ (here --)
: } compile-only immediate 0 state !b ['] wordend , here over swap - reserve execute ;

\ (val -- name --)
\ Execution of a variable: (-- addr)
: variable create , ;

\ (val -- name --)
\ Execution of a constant: (-- val)
: constant create , does> @ ;

\ (size -- name --)
\ Execution of a buffer: (-- addr)
: buffer create reserve ;

\ Measures the length of the string
( str -- len )
: strlen 0 while swap dup @b do 1+ swap 1+ done drop ;

\ Copies string to ADDR
( str addr -- )
: strcpy while swap dup @b do over over @b swap !b 1+ swap 1+ done drop drop ;

\ Idea: FFI
\ dlopen libc.so
\ 3 1 ffi socket socket dlerr? ...
\ dlclose

\ Idea: move I/O to stdlib
\ + introduce syscall
\ + define system-specific libraries
\ + include the libraries based on the detected OS


\ (fd b -- fd)
\ : f! here swap ,b 1 write -1 reserve ;
\ Temporairly allocate a buffer of 1 byte,
\ Read into it,
\ Read the byte from the buffer onto stack,
\ Unreserve the buffer
\ : f@ here swap here 0 ,b 1 read swap @b -1 reserve ;
: [eb] immediate compile-only eb ,, ;

( x -- -x)
: neg 0 swap - ;

\ realmod
( x y -- x%%y )
: %%
 over 0 <
 if % return endif
 swap neg swap % neg
;

\ TODO
(
\ : memusage here base - "%iB\n" printf ;
%u
%i
%f
)
\ Idea: one 'universal' function for stdout
\ OUT
\ ( aN ... a2 a1 str --)
\ Format:
\ %u -- unsigned
\ %i -- signed
\ %f -- floating
\ %s -- string
\ %Xa -- precision, where a=u|i|f
\ %! -- interpret last argument as a buffer to write to
\ %$ -- interpret last argument as a file descriptor to write to

