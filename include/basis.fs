: bye 0 exit ;

: != = not ;

: cell 1 cells ;

: , here ! cell reserve ;

: ,b here !b 1 reserve ;

: [ immediate compile-only 0 state !b ;

: ] 1 state !b ;

: 'lit [ ' lit dup , , ] ;

: ,, 'lit , , ;

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

( This is a
  multiline comment
  Note: multiline comments don't work in REPL.  )

\ : ." while eb dup '"' != over 0 != and do emit done drop ;

\ Writes a c-string into wordspace
\ : " here while eb dup '"' != over 0 != and do ,b done drop 0 ,b ;

\ Types out a c-string
: type while dup @b dup 0 != do emit 1 + done drop drop ;

\ Idea: :noname ;run syntax. Compiles a noname word, executes it, unreserves memory

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

: f! here swap ,b 1 write -1 reserve ;
\ Temporairly allocate a buffer of 1 byte,
\ Read into it,
\ Read the byte from the buffer onto stack,
\ Unreserve the buffer
: f@ here swap here 0 ,b 1 read swap @b -1 reserve ;


: 1+ 1 + ;
: 1- 1 - ;

\ ( A B -- B ) writes cell A to address B preserving B on stack
: !! dup rot swap ! ;

\ ( A B -- B ) writes byte A to addres B preserving B on stack
: !!b dup rot swap !b ;

: [eb] immediate compile-only eb ,, ;

\ Various tests...

: t1 [eb] j . ;

: t2 0 if 99 . 10 emit endif ;

: t3 1 if 100 . 10 emit endif ;

: t4 0 while 1 + dup 10 != do dup . 10 emit done ;

" Hello, world!"
: t5 [ ,, ] type cr ;
