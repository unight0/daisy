
const char *doc_doc = 
    "Reads the word name from source ahead.\n"
    "Attempts to access the docstring of the word.\n"
    "Displays the docstring; If none found, reports error.\n";

const char *doc_lit = "A phony word for compiling numbers.\n";
const char *doc_strlit = "A phony word for compiling strings.\n";
const char *doc_return = "A phony word for finishing words.\n";

const char *doc_exit = "(i -- )\nExit the program with code I.\n";

const char *doc_findword =
    "(-- word -- wordptr)\n"
    "Reads WORD from source ahead.\n"
    "Attempts to find the WORD in the dictionary, puts the WORDPTR onto stack.\n";    

const char *doc_immediate =
    "Marks the currently compiled word as IMMEDIATE.\n"
    "IMMEDIATE are executed immediately upon being encountered.\n"
    "Thus IMMEDIATE words are not compiled.\n"
    "IMMEDIATE words allow for metaprogramming.\n"
    "IF and ELSE are implemented as IMMEDIATE words.\n"
    "IMMEDIATE words work similarly to macros.\n"
    "Put it inside the body of the word like this:\n"
    "\t: hi interpretation-only 10 ;\n";    

const char *doc_interonly =
    "Marks the currently compiled word as interpretation-only.\n"
    "Works best with asssitant words like DOC.\n"
    "Put it inside the body of the word like this:\n"
    "\t: hi interpretation-only 10 ;\n";

const char *doc_componly =
    "Marks the currently compiled word as compile-only.\n"
    "Works best with IMMEDIATE words.\n"
    "Put it inside the body of the word like this:\n"
    "\t: hi compile-only 10 ;\n";

const char *doc_load =
    "Reads the filehint from the source ahead.\n"
    "Attempts to find the file in '.' and in INCLUDE_DIR (if defined).\n"
    "If the name is provided in \"quotes\", then it will be taken as an absolute filepath.\n";

const char *doc_branch =
    "(addr -- )\n"
    "Branch unconditionally to ADDR.\n"
    "Cannot be used properly by user, used by IF ELSE etc.\n";

const char *doc_zbranch =
    "(cond addr --)\n"
    "Branch to ADDR if cond == 0.\n"
    "Cannot be used properly by user, used by IF ELSE etc.\n";

const char *doc_colon =
    "(-- wordname --)\n"
    "Scans the name for the new word from source ahead.\n"
    "Puts the system into compilation mode.\n"
    "Use it like this:\n"
    "\t: myword 1 2 3 4 ;\n";

const char *doc_create =
    "(-- wordname --)\n"
    "Creates a new word with name WORDNAME with the following default body:\n"
    "\tLIT <ptr> RETURN\n"
    "\t      |-----------^\n"
    "Used for CONSTANT and VARIABLE.\n"
    "Use DOES> to replace the body of the newly created word.\n";

const char *doc_does =
    "(-- code ahead --)\n"
    "Reads the until ; for the code ahead\n"
    "Modifies the definition of the just CREATEd word to fit the following:\n"
    "\t <...> LIT <ptr> <code ahead> RETURN\n"
    "\t ^-----------|\n"
    "Why does the pointer point to some memory *behind* the word?\n"
    "Well, it points to the end of the previous word definition.\n"
    "Full memory layout looks more like this:\n"
    "\tLIT <ptr> RETURN <...> LIT <ptr> <code ahead> RETURN\n"
    "\t      |----------^-----------|\n"
    "This previous definition is abandoned.\n"
    "This 'leaks' some memory, but an acceptable amount.\n"
    "The size of this memory block is 0 by default, but you\n"
    "can increase its size by using RESERVE.\n"
    "Typical usage example:\n"
    "\t: constant create , does> @ ;\n"
    "Note: if you don't want the pointer to the memory block, drop it:\n"
    "\t... does> drop ... ;\n";

const char *doc_semicolon =
    "(--)\n"
    "Finishes the declaration of a new word.\n"
    "Puts the system into interpretation mode.\n";

const char *doc_eb =
    "(-- b -- b)\n"
    "Stands for 'Expression Byte'.\n"
    "Scans one byte from the source ahead; puts it onto stack.\n"
    "Example:\n"
    "\teb c . \\ prints 99\n";

const char *doc_see =
    "(-- word --)\n"
    "Reads the word name from the source ahead.\n"
    "Attempts to disassemble the word's contents.\n"
    "Will not produce informative output with predefined words.\n";

const char *doc_words =
    "(--)\n"
    "Displays the defined words and how full the dictionary is.\n";

const char *doc_flags =
    "(-- word --)\n"
    "Reads the word name from the source ahead.\n"
    "Displays flags assigned to the word.\n";

const char *doc_swap =
    "(a b -- b a)\n";

const char *doc_rot =
    "(a b c -- b c a)\n";

const char *doc_pick =
    "(AN ... A2 A1 N -- AN ... A2 A1 AN)\n";

const char *doc_dup =
    "(a -- a a)\n";

const char *doc_drop =
    "(a --)\n";

const char *doc_dropall =
    "(.. c b a --)\n";

const char *doc_over =
    "(a b -- a b a)\n";

const char *doc_arith =
    "All arithmetic operations (including = xor > etc.) work the same:\n"
    "(a b -- a[OP]b), where OP -- the operation.\n";

const char *doc_arithf =
    "This is a float variant of an arithmetic opeeration.\n"
    "All arithmetic operations (including = xor > etc.) work the same:\n"
    "\t(a b -- a[OP]b), where OP -- the operation.\n";

const char *doc_dot =
    "(int -- )\n"
    "Print an integer. Works like printf(\"%ld\", INT).\n";

const char *doc_dotf = 
    "(flt -- )\n"
    "Print a float. Works like printf(\"%f\", FLT).\n";

const char *doc_emit =
    "(b -- )\n"
    "Writes one byte to the standard output.\n";

const char *doc_rmfile =
    "(filename --)\n"
    "Removes file 'FILENAME'.\n"
    "Use FILE-EXISTS? to determine if file exists before removing.\n";

const char *doc_open =
    "(filename -- fd)\n"
    "Opens file FILENAME, returns file descriptor FD.\n"
    "Use FILE-EXISTS? to determine if file exists before opening.\n";

const char *doc_close =
    "(fd -- )\n"
    "Closes file FD.\n";

const char *doc_filex =
    "(filename -- b)\n"
    "Tests if FILENAME exists. Puts 1 on stack if yes, 0 otherwise.\n";

const char *doc_rename =
    "(oldfilename newfilename --)\n"
    "Renames OLDFILENAME to NEWFILEname\n";

const char *doc_touch =
    "(filename --)\n"
    "Creates a new empty file called FILENAME.\n";

const char *doc_trunc =
    "(fd size -- fd)\n"
    "Changes size of file FD to SIZE; preserves FD.\n";

const char *doc_fsize =
    "(fd -- fd size)\n"
    "Puts the size of the file FD onto stack; preserves FD.\n";

const char *doc_write =
    "(fd buf len -- fd)\n"
    "Writes data from buffer BUF of size LEN to file FD; preserves FD.\n"
    "Note: Use RESERVE to allocate a buffer, RESERVE with a negative value to deallocate it.\n"
    "Tip: use FD=1 for writing to stdout, FD=3 for writing to stderr.\n";

const char *doc_read =
    "(fd buf len -- fd buf)\n"
    "Reads data from FD to buffer BUF of size LEN; preserves FD and BUF.\n"
    "Note: Use RESERVE to allocate a buffer, RESERVE with a negative value to deallocate it.\n"
    "Tip: use FD=0 for reading from stdin.\n";

const char *doc_seek =
    "(fd offs -- fd)\n"
    "Moves the position pointer of FD to OFFS; preserves FD\n";

const char *doc_fetch =
    "(addr -- value)\n"
    "Reads a cell from memory pointed by ADDR.\n"
    "Note: use @b to fetch a byte.\n";

const char *doc_wr =
    "(val addr -- )\n"
    "Writes a cell with value VAL to memory pointed by ADDR.\n"
    "Note: use !b to write a byte.\n";

const char *doc_bfetch =
    "(addr -- val)\n"
    "Reads a byte from memory pointed by ADDR.\n"
    "Note: use ! to write a cell.\n";

const char *doc_bwr =
    "(val addr -- )\n"
    "Writes a byte with value VAL to memory pointed by ADDR.\n"
    "Note: use @ to read a cell.\n";

const char *doc_state =
    "(-- addr)\n"
    "Returns the addr of the byte that controls the system state.\n"
    "Write 0 to change to interpretation mode, write 1 to change to compile mode.\n";

const char *doc_cells =
    "(n -- a)\n"
    "Puts how many bytes N cells would occupy in memory onto stack.\n";

const char *doc_here =
    "(-- addr)\n"
    "Puts the addr of the current end of the wordspace onto stack.\n"
    "For clarifications about the FORTH memory model use `doc-mem`\n";

const char *doc_base =
    "(-- addr)\n"
    "Puts the addr of the beginning of the wordspace onto stack.\n"
    "For clarifications about the FORTH memory model use `doc-mem`\n";

const char *doc_tip =
    "(-- addr)\n"
    "Puts the addr of the highest address of the wordspace onto stack.\n"
    "For clarifications about the FORTH memory model use `doc-mem`\n";

const char *doc_reserve =
    "(n -- )\n"
    "Reserves N bytes at the end of wordspace.\n"
    "Use negative value to unreserve.\n"
    "However, be careful when using RESERVE in compile mode because new words are \nwritten into the same memory space!\n"
    "For clarifications about the FORTH memory model use `doc-mem`\n";

const char *doc_docmem =
    "(--)\n"
    "Show clarifications about the FORTH memory model.\n";

const char *doc_execute =
    "(begin --)\n"
    "Executes a set of instructions starting from BEGIN.\n"
    "Use this if you want to execute an anonymous word.\n";
