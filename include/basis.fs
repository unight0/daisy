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

\ : buffer create reserve ;

\ : variable here 1 reserve constant ;

( IDEA
\ v stacktop --
: >C dup @ 1+ rot swap !! swap ! ;

128 cells buffer cfs
cfs variable cfst
10 cfst >C C>
)


( This is a
  multiline comment
  Note: multiline comments don't work in REPL.  )

\ : ." while eb dup '"' != over 0 != and do emit done drop ;

\ Writes a c-string into wordspace
\ : " here while eb dup '"' != over 0 != and do ,b done drop 0 ,b ;


\ Briefly enter compilation mode, execute the
\ created word, and then delete it

\ SUPER-MEGA-ACHTUNG!!
\ { places HERE at the top of the stack, and } uses it!
\ Therefore, the top element on the stack upon executing } should be
\ the HERE left by the {
\ If you fail to comply with this, it produces a hardly-debuggable segfault!
\ Be careful with custom IMMEDIATE words!

\ ( -- here)
: { interpretation-only here 1 state !b ;
\ We are unreserving memory before we execute its contents,
\ but it is okay because we are only running one thread.
\ Additionally, when (or IF) we implement a multithreading model,
\ It will be designed in such a way that this wouldn't be an issue.
\ (here --)
: } compile-only immediate 0 state !b here over over - reserve execute  ;

\ Types out a string
: type while dup @b dup 0 != do emit 1 + done drop drop ;

\ Measures the length of the string
: strlen 0 while swap dup @b do 1+ swap 1+ done drop ;

\ ( FD STR -- )
\ Writes a string to FD
: writestr dup strlen write ;

\ Idea: FFI
\ dlopen libc.so
\ 3 1 ffi socket socket dlerr? ...
\ dlclose

: cr 10 emit ;

: space 32 ;

: newline 10 ;

\ These definitions assist OPEN
: rdonly 0 1 ;
: wronly 1 0 ;
: rdwr 1 1 ;

\ (fd b -- fd)
: f! here swap ,b 1 write -1 reserve ;
\ Temporairly allocate a buffer of 1 byte,
\ Read into it,
\ Read the byte from the buffer onto stack,
\ Unreserve the buffer
: f@ here swap here 0 ,b 1 read swap @b -1 reserve ;

: [eb] immediate compile-only eb ,, ;

\ Memory usage and memory left utilities
: memusage here base - . "B\n" type ;
: memleft tip here - . "B\n" type ;

\ TODO
(
\ : memusage here base - "%iB\n" printf ;
%u
%i
%f
)
