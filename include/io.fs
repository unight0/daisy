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
: rdonly 0 1 ;
: wronly 1 0 ;
: rdwr 1 1 ;

\ NOTE: temporairly(?) here, move later to utils.fs or smth
\ Memory usage and memory left utilities
: memusage here base - . "B\n" type ;
: memleft tip here - . "B\n" type ;
