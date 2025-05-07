load basis.fs

{
  "hello" file-exists? if
  \ error
  "Error: file already exists!" 2 swap writestr
  newline f!
  drop
  1 exit endif
}


"hello" touch

"hi" 2 write newline f!

128 seek

"fs!" 3 write

"hello" 5 write

close

1 "this is a test" 14 write cr drop
\ bye
