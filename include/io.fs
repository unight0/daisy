load basis.fs

0 constant sys-read
1 constant sys-write
2 constant sys-open
3 constant sys-close

: write 3 1 syscall ;

: writestr swap dup strlen swap rot write ;

: type 0 writestr drop ;

\ Reserve a tmp buffer of 1 char, disasterously ineffective
: emit here swap ,b 1 swap 0 write drop -1 reserve ;

: cr 10 emit ;

: space 32 ;

: newline 10 ;

\ These definitions assist OPEN
\ : rdonly 0 1 ;
\ : wronly 1 0 ;
\ : rdwr 1 1 ;


( i -- str )
\ I'm going to mark all 'local' variables by putting them in (...)
\ NOTE: this is a shit implementation. There should be no globally available variables
24 buffer (i2str-buf)
0 variable (i2str-isneg)
: i2str

  \ Negate the number and remember that it was negative
  0 (i2str-isneg) !
  dup 0 < if neg 1 (i2str-isneg) ! endif 

  \ Finish c-str with '\0'
  0 (i2str-buf) 23 + !b

  dup 0 = if drop '0' (i2str-buf) 22 + !!b return endif


  (i2str-buf) 22 + \ Begin from the right
  swap
  while dup 0 != do
    dup 10 %
    '0' +
    rot dup rot swap
    !b
    1 -
    swap 10 /
  done
  drop
  (i2str-isneg) @ if
    '-' swap !!b return
  endif
  1 +
;

: . i2str type ;


\ NOTE: temporairly(?) here, move later to utils.fs or smth
\ Memory usage and memory left utilities
: memusage here base - . "B\n" type ;
: memleft tip here - . "B\n" type ;
