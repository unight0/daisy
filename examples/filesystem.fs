load basis.fs

\ "hello\n"
\ { [ swap ,, ] type }


"Error: file already exists!"
"hello"
{ 
  [ swap ,, ] dup file-exists? if
  \ error
  [ rot ,, ] 2 swap writestr
  newline f!
  drop
  1 exit endif
}

( {
  "Error: file already exists!" dup file-exists? if
  \ error
  "hello" 2 swap writestr
  newline f!
  drop
  1 exit endif
} )


\ "error" HERE "hello"

\ drop drop

"hello" touch

"hi" 2 write newline f!

128 seek

"fs!" 3 write

"hello" 5 write

close

1 "this is a test" 14 write cr drop
bye
