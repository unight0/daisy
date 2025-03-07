: != = not ;

: cell 1 cells ;

: , here ! cell reserve ;

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

: [eb] immediate compile-only eb ,, ;

: t1 [eb] j . ;

: t2 0 if 99 . 10 emit endif ;

: t3 1 if 100 . 10 emit endif ;

: t4 0 while 1 + dup 10 != do dup . 10 emit done ;
