load basis.fs

"Error: file already exists!"
"hello"
: preambule [ ,, ] dup file-exists? if
          \ error
	      [ over ,, ] 2 swap 27 write
	      newline f!
	      drop
	      1 exit endif ;

preambule

drop drop

"hello" touch

"hi" 2 write newline f!

128 seek

"fs!" 3 write

"hello" 5 write

close

1 "this is a test" 14 write cr drop
bye
