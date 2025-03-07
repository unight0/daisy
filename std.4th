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

: \ while eb dup 10 != swap 0 != and do done ;
\ This is a comment!

: ." while eb dup '"' != over 0 != and do emit done drop ;

\ Writes a c-string into wordspace
: " here while eb dup '"' != over 0 != and do ,b done drop 0 ,b ;

\ Types out a c-string
: type while dup @b dup 0 != do emit 1 + done drop drop ;

: [eb] immediate compile-only eb ,, ;

\ Various tests...

: t1 [eb] j . ;

: t2 0 if 99 . 10 emit endif ;

: t3 1 if 100 . 10 emit endif ;

: t4 0 while 1 + dup 10 != do dup . 10 emit done ;
