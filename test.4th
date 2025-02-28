: , here ! 1 cells reserve ;
: ahead here 0 , ; 'n' 'o' '!'
: if immediate ahead postpone 0branch ;
: then immediate here swap ! ;

: hi 0 if 100 then ;

: \ immediate here eb dup 0 = swap 10 = or 0branch;
