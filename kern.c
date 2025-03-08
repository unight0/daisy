#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include <unistd.h> //access()
#include <fcntl.h>
#include <sys/stat.h>

typedef void (*SysWord)(void);
typedef struct Word Word;
// TODO: x86 - x64 portability
//typedef int64_t isize_t;
//typedef uint64_t usize_t;
typedef union {
    int64_t i;
    uint64_t u;
    double f;
    char *s;
    void *p;
    Word *w;
} Cell;

struct Word {
    char flags;
    //size_t size;
    Cell *end;
    //char signature;
    union {
        Cell *body;
        SysWord sw;
    };
};

enum {
    FL_IMMEDIATE  = (1<<0),
    FL_COMPONLY   = (1<<1),
    FL_INTERONLY  = (1<<2),
    FL_PREDEFINED = (1<<3),
};

#define SYSWORD(s,fl) (Word){FL_PREDEFINED|(fl),0,.sw=(s)}

// A phony word for compiling numbers
void lit_w() {}

// Set in init_dict()
Word *lit_word = NULL;

#define STACK_SIZE 128
// Stack && topmost element
Cell stack[STACK_SIZE], *stacktop = stack;

// Name of the currently compiled word
char *comptarget = NULL;

// Dictionary entries point to the wordspace areas (apart from predefined words)
#define DICT_SIZE 256
typedef struct { char *name; Word w; } DictEntry;
//NOTE: DictEntry *userwords is not yet used
DictEntry dict[DICT_SIZE] = {0}, *dicttop = dict, *userwords = dict;


// Memory space for words. Allocation for user data is allowed
// too (through reserve). Memory release is available (though limited) through
// supplying negative values to reserve
// protection.
// TODO: malloc it (?)
// WSBLOCKS_SIZE = size of one word space memory block
#define WSBLOCK_SIZE sizeof(Word)*DICT_SIZE*2
char wordspace[WSBLOCK_SIZE] = {0}, *wspacend = wordspace;
//char *wordspace, *wspacend;

// Expression that is processed at the moment
char *expression = NULL;
// Pointer to a char in expression
char *pointer = NULL;

// 0 = interpreting
// 1 = compiling
char state = 0;
// Absolute jump for branch and 0branch
// NOTE(2): this is basically timetravel
int need_jump = 0;
Cell *absjump = 0;

// For errors 
char *lastword = NULL;

size_t line = 0;
// Display err instead of ok
int error_happened = 0;
// Error reporitng
#define error(cs, ...){							\
	error_happened = 1;						\
	fprintf(stderr, "\033[m\033[1mAt %lu:%lu, '%s': ", line, pointer-expression, lastword); \
	fprintf(stderr, cs, ##__VA_ARGS__);				\
	fprintf(stderr, "\033[m");}					\

// Report insufficient arguments
void ins_arguments(const char *who, size_t req, size_t got) {
    error("In %s: expected %lu got %lu\n", who, req, got);
}

// Ask to have NUM arguments on stack
int askstack(const char *who, size_t num) {
    if ((size_t)(stacktop-stack) < num) {
        ins_arguments(who, num, (stacktop-stack));
        return 1;
    }
    return 0;
}

#define ASKSTACK(num) askstack(__func__, num)

// Ask to have NUM space left on stack arguments on stack
// Prevents stack overflow
int askspace(size_t num) {
    const size_t current_size = (stacktop-stack);
    const size_t leftover = STACK_SIZE - current_size;

    if (leftover < num) {
        error("Error: stack overflow\n");
        return 1;
    }

    return 0;
}

/*
  // TODO: wordspace enlargement. Allocate new memory block and set wspacend=wordspace=<newblock>
  // Keep a list of allocated memory blocks. Free on exit.
#include <sys/mman.h>
void wspacealloc() {
    // NOTE: change MAP_PRIVATE to MAP_SHARED to support multiuser access to the running FORTH system
    wordspace = mmap(NULL, WSBLOCK_SIZE, PROT_READ|PROT_WRITE|PROT_EXEC, MAP_PRIVATE|MAP_ANON, -1, 0);
    wspacend = wordspace;


    if (wordspace == MAP_FAILED) {
	perror("Initial memory allocation failed");
	exit(1);
    }
}
void wspacenlarge(size_t bytes) {
    size_t endoffs = wspacend-wordspace;
    char *tmp = wordspace;
}
void wspacedealloc() {
    if(munmap(wordspace, WSBLOCK_SIZE)) {
	perror("Final memory deallocation failed");
	exit(1);
    }
}
*/

/* Begin predefined words *******/

// Note: also called from main()
void bye(int c) {
    for (DictEntry *p = userwords; p < dicttop; p++) {
        free(p->name);
    }
    //wspacedealloc();
    exit(c);
}

void exit_w() {
    if(ASKSTACK(1)) return;

    int c = stacktop--->i;
    
    bye(c);
}

// Pushes address of the state variable onto stack
void state_w() {
    if(askspace(1)) return;

    (++stacktop)->p = &state;
}

// N -- 
void branch_w() {
    if (ASKSTACK(1)) return;

    Cell n = *stacktop--;

    need_jump = 1;
    absjump = (Cell*)n.p;
}

// COND N --
// Branches when COND == 0
void zbranch_w() {
    if (ASKSTACK(2)) return;

    Cell pos  = *stacktop--;
    Cell cond = *stacktop--;

    if(cond.i) return;

    need_jump = 1;
    absjump = (Cell*)pos.p;
}

// Wrapper for find_word()
char *parse(void);
Word *find_word(char*);
void findword_w() {
    if(askspace(1)) return;

    char *word = parse();

    if (word == NULL || !*word) {
        error("No name provided for '\n");
        return;
    }

    Word *w = NULL;

    //NOTE: copied the entirety of find_word() to here because we don't want the
    //error message to pop up here. TODO: Is this needed?
    // Uppercase
    for (char *p = word; *p; ++p) *p = toupper(*p);

    // Search
    for(DictEntry *p = dict; p < dicttop; ++p) {
        if(!strcmp(p->name, word)) { 
            w = &(p->w);
            break;
        }
    }

    (++stacktop)->p = (void*)w;
}

// Pushes pointer to the topmost wspace element
void here_w() {
    if (askspace(1)) return;
    
    (++stacktop)->p = (void*)wspacend;
}

// Reads one byte
void bfetch_w() {
    if (ASKSTACK(1)) return;
    if (askspace(1)) return;

    char *ptr = (char*)stacktop--->p;

    (++stacktop)->i = *ptr;
}

// Writes one byte
void bwr_w() {
    if (ASKSTACK(2)) return;

    char *addr = stacktop--->p;
    char byte = (char)stacktop--->i;

    *addr = byte;
}

// Reads one cell
void fetch_w() {
    if (ASKSTACK(1)) return;
    if (askspace(1)) return;

    Cell *ptr = (Cell*)stacktop--->p;

    *++stacktop = *ptr;
}

// Writes one cell
void wr_w() {
    if (ASKSTACK(2)) return;

    Cell *addr = stacktop--->p;
    Cell c = *stacktop--;

    *addr = c;
}

// Reserves n bytes in wordspace
// Strictly allocates, doesn't deallocate
void reserve_w() {
    if (ASKSTACK(1)) return;
    
    size_t bytes = stacktop--->i; 

    wspacend += bytes;
}

// Pushes the amount of bytes occupied by N cells
// N -- B
void cells_w() {
    if (ASKSTACK(1)) return;

    size_t cells = stacktop--->u; 

    (++stacktop)->u = cells*sizeof(Cell);
}

//void readfile_w() {
//    
//}

// DRY!
#define ASKFILENAME(filename)\
    if(ASKSTACK(1)) return;\
    char *filename = stacktop--->s;\
    if (filename == NULL) {\
	error("NULL as filename string\n");\
	return; }

// filename WR? RD? -- fd
void open_w() {
    if (ASKSTACK(2)) return;

    int rd = stacktop--->u,
        wr = stacktop--->u;

    int flags = 0;

    if (rd && wr) flags = O_RDWR;
    else if (rd) flags = O_RDONLY;
    else if (wr) flags = O_WRONLY;
    else {
	error("Invalid combination of RD and WR flags\n");
	return;
    }
    
    ASKFILENAME(filename);
    
    int fd = open(filename, flags);

    if (fd == -1) {
	error("Couldn't open file '%s'\n", filename);
	perror("");
    }

    (++stacktop)->i = fd;
}

// Creates a new file and opens it
// FILENAME -- FD
void touch_w() {
    ASKFILENAME(filename);

    int fd = open(filename, O_CREAT|O_RDWR|O_TRUNC, 0644);

    if (fd == -1) {
	error("Could'n touch '%s': ", filename);
	perror("");
    }

    // Return -1
    (++stacktop)->i = fd;
}

// Closes file
// FD --
void close_w() {
    if (ASKSTACK(1)) return;

    int fd = stacktop--->i;

    if(close(fd)) {
	error("Couldn't close file: ");
	perror("");
    }
}

// Queries file size
// FD -- FD SIZE
void filesize_w() {
    if (askspace(1)) return;

    int fd = stacktop->i;

    struct stat statbuf;
    
    if (fstat(fd, &statbuf)) {
	error("Couldn't stat file: ");
	perror("");
	return;
    }

    (++stacktop)->u = statbuf.st_size;
}

// Resizes file
// FD SIZE -- FD
void trunc_w() {
    if (ASKSTACK(2)) return;

    size_t sz = stacktop--->u;
    int fd = stacktop->i;

    printf("Truncating to %lu\n", sz);
    if (ftruncate(fd, sz)) {
	error("Couldn't truncate(resize) file: ");
	perror("");
    }
}

// Write byte to file
// FD B -- FD
void fwb_w() {
    if (ASKSTACK(2)) return;

    char byte = stacktop--->u;

    int fd = stacktop->i;

    if(write(fd, &byte, 1) == -1) {
	error("Couldn't write to file: ");
	perror("");
    }
}

// Writes to file
// FD BUF LEN -- FD
void write_w() {
    if (ASKSTACK(3)) return;

    size_t len = stacktop--->u;

    void *buf = stacktop--->p;

    int fd = stacktop->i;

    if(write(fd, buf, len) == -1) {
	error("Couldn't write to file: ");
	perror("");
    }
}

// Fetch byte from file
// FD -- FD
void ffetch_w() {
    if (ASKSTACK(1)) return;

    int fd = stacktop->i;
    
    char buf;

    if(read(fd, &buf, 1)) {
	error("Couldn't read from file: ");
	perror("");
    }

    (++stacktop)->i = buf;
}

// Read from file
// FD BUF LEN -- FD BUF
void read_w() {
    if (ASKSTACK(3)) return;

    size_t len = stacktop--->u;
    
    void *buf = stacktop--->p;
    
    int fd = stacktop->i;


    if(read(fd, &buf, len)) {
	error("Couldn't read from file: ");
	perror("");
    }

    (++stacktop)->p = buf;
}

// Set offset to N
// FD OFFS - FD
void seek_w() {
    if (ASKSTACK(2)) return;

    off_t offs = stacktop--->u;

    int fd = stacktop->i;

    lseek(fd, offs, SEEK_SET);
}

/*
// Maps file for access
// FILENAME -- FD ADDR SIZE
#include <sys/mman.h>
void mapfile_w() {
    if(ASKSTACK(1)) return;
    if(askspace(2)) return;

    int fd = stacktop--->i;

    // Query file size
    struct stat statbuf;

    if (fstat(fd, &statbuf)) {
	error("Couldn't stat file: ");
	perror("");
	return;
    }

    int flags = fcntl(fd, F_GETFL);

    if (flags == -1) {
	error("Could not get fd flags: ");
	perror("");
	return;
    }

    int prot = 0;
    
    if ((flags & O_ACCMODE) != O_WRONLY) prot |= PROT_READ;
    if ((flags & O_ACCMODE) != O_RDONLY) prot |= PROT_WRITE;

    // Do we need to have PROT_EXEC here? It may be unsafe. At the same time, I don't want to put user in a situation
    // where they need to execute the file but cannot due to interface that doesn't provide enough control
    // Maybe ask for permissions as an argument
    //void *file = mmap(NULL, statbuf.st_size, prot, MAP_SHARED, fd, 0);
    void *file = mmap(NULL, statbuf.st_size, prot, MAP_SHARED, fd, 0);
    
    if (file == MAP_FAILED) {
	error("Couldn't map file: ");
	perror("");
	return;
    }
    
    (++stacktop)->p = file;
    (++stacktop)->u = statbuf.st_size;
}

// Unmaps and closes file
void unmapfile_w() {
    if(ASKSTACK(3)) return;

    size_t sz = stacktop--->u;
    void *file = stacktop--->p;

    if (munmap(file, sz)) {
	error("Couldn't unmap file: ");
	perror("");
    }
}
*/

void rmfile_w() {
    ASKFILENAME(filename);
    
    if(remove(filename)) {
	error("Couldn't delete file '%s'\n", filename);
	perror("");
    }
}

// Replace by ACCESS
void filee_w() {
    ASKFILENAME(filename);

    // Returns 0 if exists, returns -1 if doesn't
    (++stacktop)->u = !access(filename, F_OK);
}

void putint_w() {
    if (ASKSTACK(1)) return;
    printf("%ld", stacktop--->i);
}
void putflt_w() {
    if(ASKSTACK(1)) return;
    printf("%f", stacktop--->f);
}
void emit_w() {
    if(ASKSTACK(1)) return;
    putchar(stacktop--->i);
}

void exprbyte_w() {
    if(askspace(1)) return;

    (++stacktop)->i = *pointer;
    
    // Advance pointer if not EOL
    if(*pointer) pointer++;
}

void words_w() {
    for(DictEntry *p = dict; p < dicttop; p++) {
        printf("%s ", p->name);
    }
    printf("(total %lu/%d)\n", dicttop-dict, DICT_SIZE);
}

void putflags(char *word, Word *w) {
    if (!w->flags) {
        printf("'%s' = 0 --> no flags\n", word);
        return;
    }

    printf("'%s' = %d:\n", word, w->flags);

    if (w->flags & FL_PREDEFINED)
        printf("PREDEFINED\n");
    if (w->flags & FL_IMMEDIATE)
        printf("IMMEDIATE\n");
    if (w->flags & FL_COMPONLY)
        printf("COMPILE-TIME ONLY\n");
    if (w->flags & FL_INTERONLY)
        printf("INTERPRETATION-ONLY\n");
}

char *parse();
Word *find_word(char*);
void flags_w() {
    char *word = parse();

    if (word == NULL || !*word) {
        error("No word provided for FLAGS\n");
        return;
    }

    Word *w = find_word(word);

    if (w == NULL) {
        error("Word not found: '%s'\n", word);
        return;
    }

    putflags(word, w);
}

// Adds entry if not present
// TODO: store entries in wordspace
void dict_entry(char *entry) {
    assert(entry != NULL);

    for (DictEntry *p = dict; p < dicttop; p++) {
        // Already present
        // NOTE: abandons previous word declaration, which leaks memory. However, we
        // don't redefine words often (hopefully)...
        if (!strcmp(p->name, entry)) {
            p->w.body = (Cell*)wspacend;

            /* Replace to avoid memory leak*/
            free(p->name);
            p->name = entry;
            return;
        }
    }


    // Add entry
    dicttop->name = entry;
    dicttop->w.body = (Cell*)wspacend;
    dicttop++;
}

Word *find_word(char *word) {
    // (convert to uppercase)
    for (char *p = word; *p; ++p) *p = toupper(*p);
    // Search dictionary entries
    for(DictEntry *p = dict; p < dicttop; ++p) {
        //printf("%s =? %s\n", word, p->name);
        if(!strcmp(p->name, word)) return &(p->w);
    }
    
    error("'%s'?\n", word);
    return NULL;
}

char *parse();
void colon_w() {
    char *name = parse();

    if (name == NULL) {
        error( "Error: No word provided for ':'\n");
        return;
    }

    // Capitalize
    for (char *np = name; *np; ++np) {
        *np = toupper(*np);
    }

    // Copy name
    char *dictname = malloc(strlen(name) + 1);
    strcpy(dictname, name);

    dict_entry(dictname);
    comptarget = dictname;

    // Enter compile mode
    state = 1;
}

void scolon_w() {
    // Finish the word declaration
    // NOTE: careful! potential write at NULL
    find_word(comptarget)->end = (Cell*)wspacend;
    wspacend++;

    comptarget = NULL;

    // Back into interpretation mode
    state = 0;
}

//TODO: these flag-words should be put AFTER the word,
//not inside of it
// Adds FL_IMMEDIATE to the currently compiled word
void immediate_w() {
    find_word(comptarget)->flags |= FL_IMMEDIATE;
}

// Adds FL_COMPONLY to the currently compiled word
void compile_only_w() {
    find_word(comptarget)->flags |= FL_COMPONLY;
}

// Adds FL_INTERONLY to the currently compiled word
void interpretation_only_w() {
    find_word(comptarget)->flags |= FL_INTERONLY;
}

// A B -- B A
void swap_w(void) {
    if(ASKSTACK(2)) return;

    Cell b = *stacktop--;
    Cell a = *stacktop--;

    *(++stacktop) = b;
    *(++stacktop) = a;
}

// A --
void drop_w(void) {
    if(ASKSTACK(1)) return;
    --stacktop;
}

// A -- AA
void dup_w(void) {
    if(ASKSTACK(1)) return;

    Cell a = *stacktop;

    *(++stacktop) = a;
}

// A B -- A B A
void over_w(void) {
    if(ASKSTACK(2)) return;

    Cell b = *stacktop--;
    Cell a = *stacktop--;

    *(++stacktop) = a;
    *(++stacktop) = b;
    *(++stacktop) = a;
}

// A B C -- B C A
void rot_w(void) {
    if(ASKSTACK(3)) return;

    Cell c = *stacktop--;
    Cell b = *stacktop--;
    Cell a = *stacktop--;

    *(++stacktop) = b;
    *(++stacktop) = c;
    *(++stacktop) = a;
}

/* Boring repetative arithmetic definitions below */

// Macros make it less repetative
#define TWOOP(a,b,t,v) if(ASKSTACK(2))return;\
    Cell a = *stacktop--;                    \
    Cell b = *stacktop--;                    \
    (++stacktop)->t = (v);


// Should word for all data types
void equ_w(void) {
    TWOOP(b, a, i, a.i == b.i);
}
void gre_w(void) {
    TWOOP(b, a, i, a.i > b.i)
}
void les_w(void) {
    TWOOP(b, a, i, a.i < b.i);
}
void mod_w(void) {
    TWOOP(b, a, i, a.i % b.i);
}
void sum_w(void) {
    TWOOP(b, a, i, a.i + b.i);
}
void sub_w(void) {
    TWOOP(b, a, i, a.i - b.i);
}

void div_w(void) {
    if(ASKSTACK(2))return;

    Cell b = *stacktop--;                    
    Cell a = *stacktop--;                    

    if (b.i == 0) {                          
        error("Division by zero\n");         
        return;                              
    }                                        

    (++stacktop)->i = a.i/b.i;                   
}
void mul_w(void) {
    TWOOP(b, a, i, a.i * b.i);
}

/* Floating arithmetics */
void fsum_w(void) {
    TWOOP(b, a, f, a.f + b.f);
}
void fsub_w(void) {
    TWOOP(b, a, f, a.f - b.f);
}
void fdiv_w(void) {
    if(ASKSTACK(2)) return;

    Cell b = *stacktop--;                    
    Cell a = *stacktop--;                    

    if (b.f == 0) {                          
        error("Floating division by zero\n");         
        return;                              
    }                                        

    (++stacktop)->f = a.f/b.f;                   
}
void fmul_w(void) {
    TWOOP(b, a, f, a.f * b.f);
}

/* Logic */
void and_w(void) {
    TWOOP(b, a, i, a.i && b.i);
}
void or_w(void) {
    TWOOP(b, a, i, a.i || b.i);
}
void xor_w(void) {
    TWOOP(b, a, i, (!a.i && b.i) || (a.i && !b.i));
}
void not_w(void) {
    if(ASKSTACK(1)) return;

    Cell c = *stacktop--;

    (++stacktop)->i = !c.i;
}

/* End predefined words ********/
// Prompt
void ok() {
    if(error_happened) {
        error_happened = 0;
        printf("\033[1merr\033[m\n");
        return;
    }
    const size_t stack_sz = (stacktop-stack);

    char *cm = "";

    if (state) cm = "(c)";

    if (stack_sz)
        printf("\033[1m%sok %lu\033[m\n", cm, stack_sz);
    else
        printf("\033[1m%sok\033[m\n", cm);
}

void prompt() {
    if (state)
        printf("[C] ");
    else printf("[I] ");
}

char *parse() {

    // Remember last word processed for errors

    // End of line
    if (!*pointer) return NULL;

    // Skip spaces
    for(;isspace(*pointer);++pointer) if (*pointer == '\n') line++;

    char *word = lastword = pointer;

    // Skip until next space
    for(;!isspace(*pointer) && *pointer;++pointer);

    // Put endline
    if (*pointer) {
        *pointer = 0;
        ++pointer;
    }

    return word;
}

// 0 -- word
// 1 -- integer
// 2 -- float
// 3 -- char
char identify(char* word) {
    int dotcount, signcount, cc;
    dotcount = signcount = cc = 0;

    // 'c' -- for char
    if (strlen(word) == 3 && *word == '\'' && word[2] == '\'') {
        return 3;
    }

    for(;*word;++word,++cc) {
        if (*word == '.') {
            dotcount++;
            continue;
        }
        if (*word == '-') {
            signcount++;
            continue;
        }
        if (!isdigit(*word)) {
            return 0;
        }
    }

    // Invalid numbers
    if (signcount > 1) return 0;
    if (dotcount > 1) return 0;
    if (dotcount == cc) return 0;

    // Float
    if (dotcount == 1) return 2;

    // Integer
    return 1;
}

// Compilation
void compile(Word *w) {
    // Check that the word can be compiled
    if (w->flags & FL_INTERONLY) {
        error("Word is runtime-only!\n");
        return;
    }

    // Append the word
    *(Cell*)wspacend = (Cell){.w=w};
    wspacend += sizeof(Cell);
}
    
void compile_number(char *word, char numericity) {
    Cell num;

    // Convert
    if (numericity == 1)
        num.i = atol(word);
    else if (numericity == 2)
        num.f = atof(word);
    else
        num.i = word[1];

    // Append the number
    // LIT
    *(Cell*)wspacend = (Cell){.w=lit_word};
    wspacend += sizeof(Cell);

    // Number itself
    *(Cell*)wspacend = num;
    wspacend += sizeof(Cell);
}

// Execute a thing from a userword
void execute(Word *w);
void execute_thing(Cell c) {
    static int is_cell = 0;

    if (is_cell) {
        is_cell = 0;
        // Ensure cat there is no stack overflow
        if (askspace(1)) exit(1);
        // Push
        *++stacktop = c;
        return;
    }

    // System word
    if (c.w->flags & FL_PREDEFINED) {
        // A phony pointer signals that the next Cell is data 
        if (c.w->sw == lit_w) is_cell = 1;
        else c.w->sw();
        return;
    }

    // Userword
    // Note: recursive
    // TODO: rewrite non-recursively (?)
    execute(c.w); 
}

// Interpretation
void execute(Word *w) {
    assert(w != NULL);

    // Predefined words
    if (w->flags & FL_PREDEFINED) {
        if (w->sw == lit_w) {
            error("LIT is phony word and cannot be executed\n");
            return;
        }
        w->sw();
        return;
    }

    Cell *t;
    for (t = w->body; t < w->end; t++) {
	//printf("Executing at %p\n", (void*)t);
        execute_thing(*t);
	
        if (need_jump) {
	    //printf("-- Received need_jump to %p\n", (void*)absjump);
	    //if (absjump == w->end) printf("-- This is the w->end\n");
	    need_jump = 0;
	    if (absjump < w->body || absjump > w->end) {
                error("Jump outside of body: %p\n", (void*)absjump);
                break;
            }

            t = absjump;

	    // Allow programs to branch to w->end and thus end execution
	    t--;
            continue;
        }
    }
    //printf("Ended execution at %p\n", (void*)t);
}


void push_number(char *word, char numericity) {
    // Is a number
    assert(numericity);

    if (askspace(1)) exit(1);

    // Integer
    if (numericity == 1)
        (++stacktop)->i = atol(word);

    // Float
    else if (numericity == 2)
        (++stacktop)->f = atof(word);

    // Char
    else (++stacktop)->i = word[1];
}


// Types of words:
// normal
// immediate
// compile-only
// runtime-only
// immediate, compile-only
// immediate, runtime-only (doesn't really exist)
// runtime-only, compile-only (invalid)
int flagcheck(Word *w) {
    // Invalid word
    if (w->flags & FL_INTERONLY & FL_COMPONLY) {
        error("Word is both compile-only and runtime-only\n");
        return 1;
    }
            
    // Compiling
    if (state) {
        // Interpretation-only
        if (w->flags & FL_INTERONLY) {
            error("Word is interpretation-only\n");
            return 1;
        }
        return 0;
    }

    // Executing

    // Should not be executed
    if (w->flags & FL_COMPONLY) {
        error("Word is compile-only\n");
        return 1;
    }

    return 0;
}

void eval() {
   while(1) {
       char *word = parse();
       // EOL
       if (word == NULL) break;
       if (*word == 0) break;

       //TODO: split into runtime() and compiletime() as follows.
       //if (state) runtime(word);
       //else compiletime(word);
       
       // Identify type
       char numericity = identify(word);

       // Numbers are just pushed to stack
       if (numericity) {
           if (state) compile_number(word, numericity);
           else push_number(word, numericity);
           continue;
       }

       Word *w = find_word(word);

       // Could not find the word
       if (w == NULL) return;

       if (flagcheck(w)) continue;

       if (state && !(w->flags & FL_IMMEDIATE)) compile(w);
       else execute(w);
   }
}

// Find a name by word definition
const char *reverse_search(Word *w) {
    int c = 0;
    for (DictEntry *p = dict; p < dicttop; ++p, ++c) {
        //printf("%p =? %p -- ", w, &(p->w));
        if (w == &(p->w)) return p->name;
    }
    //printf("%lu\n", c);
    //error("Reverse search failed\n");
    return NULL;
}

// Displays the word definition
// TODO: implement in userspace
void decompile(Word *w) {
    int last_lit = 0;
    //printf("WORD SIZE: %lu\n", w->end-w->body);

    for (Cell *p = w->body; p < w->end; p++) {

        const char *name = reverse_search(p->w);

        if (name == NULL && last_lit) {
            last_lit = 0;
            printf("%lu ", p->i);
            //printf("%p ", p->w);
        }

        else if (name == NULL) printf("<?(%p)> ", p->p);

        else {
            if (p->w == lit_word) last_lit = 1;
            //printf("%s{%p} ", name, p);
            printf("%s ", name);
        }
    }
    putchar('\n');
}

void see_w() {
    if(askspace(1)) return;

    char *name = parse();

    if (name == NULL || !*name) {
        error("No word provided for SEE\n");
        return;
    }

    Word *w = find_word(name);

    if (w == NULL) {
        error("Word not found: %s\n", name);
        return;
    }

    putflags(name, w);

    if (w->flags & FL_PREDEFINED) {
        printf("<asm>\n");
        return;
    }

    decompile(w);
}


// Dictionary initialization
void init_dict() {
    /* Basics */
    *dicttop++ = (DictEntry){"EXIT", SYSWORD(exit_w,0)};
    *dicttop++ = (DictEntry){"LIT", SYSWORD(lit_w,FL_IMMEDIATE)};
    lit_word = &(dicttop-1)->w;
    *dicttop++ = (DictEntry){"BRANCH", SYSWORD(branch_w,FL_COMPONLY)};
    *dicttop++ = (DictEntry){"0BRANCH", SYSWORD(zbranch_w,FL_COMPONLY)};
    *dicttop++ = (DictEntry){"'", SYSWORD(findword_w,0)};
    *dicttop++ = (DictEntry){":", SYSWORD(colon_w,FL_IMMEDIATE|FL_INTERONLY)};
    *dicttop++ = (DictEntry){";", SYSWORD(scolon_w,FL_IMMEDIATE|FL_COMPONLY)};
    *dicttop++ = (DictEntry){"IMMEDIATE", SYSWORD(immediate_w,FL_IMMEDIATE|FL_COMPONLY)};
    *dicttop++ = (DictEntry){"INTERPRETATION-ONLY", SYSWORD(interpretation_only_w,FL_IMMEDIATE|FL_COMPONLY)};
    *dicttop++ = (DictEntry){"COMPILE-ONLY", SYSWORD(compile_only_w,FL_IMMEDIATE|FL_COMPONLY)};

    /* Memory */
    *dicttop++ = (DictEntry){"HERE", SYSWORD(here_w,0)};
    *dicttop++ = (DictEntry){"STATE", SYSWORD(state_w,0)};
    *dicttop++ = (DictEntry){"@B", SYSWORD(bfetch_w,0)};
    *dicttop++ = (DictEntry){"!B", SYSWORD(bwr_w,0)};
    *dicttop++ = (DictEntry){"@", SYSWORD(fetch_w,0)};
    *dicttop++ = (DictEntry){"!", SYSWORD(wr_w,0)};
    *dicttop++ = (DictEntry){"RESERVE", SYSWORD(reserve_w,0)};
    *dicttop++ = (DictEntry){"CELLS", SYSWORD(cells_w,0)};

    /* File management */
    //*dicttop++ = (DictEntry){"READFILE", SYSWORD(readfile_w,0)};
    *dicttop++ = (DictEntry){"OPEN", SYSWORD(open_w,0)};
    *dicttop++ = (DictEntry){"TOUCH", SYSWORD(touch_w,0)};
    *dicttop++ = (DictEntry){"TRUNC", SYSWORD(trunc_w,0)};
    *dicttop++ = (DictEntry){"F!", SYSWORD(fwb_w,0)};
    *dicttop++ = (DictEntry){"F@", SYSWORD(ffetch_w,0)};
    *dicttop++ = (DictEntry){"WRITE", SYSWORD(write_w,0)};
    *dicttop++ = (DictEntry){"READ", SYSWORD(read_w,0)};
    *dicttop++ = (DictEntry){"SEEK", SYSWORD(seek_w,0)};
    *dicttop++ = (DictEntry){"FILESIZE", SYSWORD(filesize_w,0)};
    *dicttop++ = (DictEntry){"CLOSE", SYSWORD(close_w,0)};
    *dicttop++ = (DictEntry){"FILE-EXISTS?", SYSWORD(filee_w,0)};
    *dicttop++ = (DictEntry){"RMFILE", SYSWORD(rmfile_w,0)};

    // Very efficient, but introduces its own problems that overpower
    // the benefits
    //*dicttop++ = (DictEntry){"MAPFILE", SYSWORD(mapfile_w,0)};
    //*dicttop++ = (DictEntry){"UNMAPFILE", SYSWORD(unmapfile_w,0)};

    
    /* Reading from expression itself */
    // Receives 1 next byte from expression
    *dicttop++ = (DictEntry){"EB", SYSWORD(exprbyte_w,0)};

    /* Development */
    *dicttop++ = (DictEntry){"WORDS", SYSWORD(words_w,0)};
    *dicttop++ = (DictEntry){"FLAGS", SYSWORD(flags_w,0)};
    *dicttop++ = (DictEntry){"SEE", SYSWORD(see_w,0)};

    /* Stack manipulation */
    // TODO: implement PICK
    *dicttop++ = (DictEntry){"SWAP", SYSWORD(swap_w,0)};
    *dicttop++ = (DictEntry){"DROP", SYSWORD(drop_w,0)};
    *dicttop++ = (DictEntry){"DUP", SYSWORD(dup_w,0)};
    *dicttop++ = (DictEntry){"OVER", SYSWORD(over_w,0)};
    *dicttop++ = (DictEntry){"ROT", SYSWORD(rot_w,0)};

    /* I/O */
    //TODO: emit and . are not primitive enough;
    //Introduce read() and write() and file descriptors and the build . and
    //emit on top of that
    *dicttop++ = (DictEntry){".", SYSWORD(putint_w,0)};
    *dicttop++ = (DictEntry){".F", SYSWORD(putflt_w,0)};
    *dicttop++ = (DictEntry){"EMIT", SYSWORD(emit_w,0)};


    /* Logic */
    //TODO: bitwise operations
    *dicttop++ = (DictEntry){"AND", SYSWORD(and_w,0)};
    *dicttop++ = (DictEntry){"OR", SYSWORD(or_w,0)};
    *dicttop++ = (DictEntry){"NOT", SYSWORD(not_w,0)};
    *dicttop++ = (DictEntry){"XOR", SYSWORD(xor_w,0)};

    /* Arithmetics */
    *dicttop++ = (DictEntry){"=", SYSWORD(equ_w,0)};
    *dicttop++ = (DictEntry){">", SYSWORD(gre_w,0)};
    *dicttop++ = (DictEntry){"<", SYSWORD(les_w,0)};
    *dicttop++ = (DictEntry){"%", SYSWORD(mod_w,0)};
    *dicttop++ = (DictEntry){"+", SYSWORD(sum_w,0)};
    *dicttop++ = (DictEntry){"-", SYSWORD(sub_w,0)};
    *dicttop++ = (DictEntry){"/", SYSWORD(div_w,0)};
    *dicttop++ = (DictEntry){"*", SYSWORD(mul_w,0)};
    *dicttop++ = (DictEntry){"+F", SYSWORD(fsum_w,0)};
    *dicttop++ = (DictEntry){"-F", SYSWORD(fsub_w,0)};
    *dicttop++ = (DictEntry){"/F", SYSWORD(fdiv_w,0)};
    *dicttop++ = (DictEntry){"*F", SYSWORD(fmul_w,0)};

    userwords = dicttop;
}


void repl() {
   char *line = NULL;
   size_t line_sz = 0;

   // Loop
   while (1) {
       // Read
       //prompt();
       printf("\033[3m");
       getline(&line, &line_sz, stdin);
       printf("\033[m");
       if (feof(stdin)) {
           free(line);
           return;
       }
       if (ferror(stdin)) {
           perror("REPL");
           free(line);
           exit(1);
       }

       // Eval
       pointer = expression = line;
       eval();

       free(line);
       pointer = expression = line = NULL;

       // Print
       ok();
    }
}

char *load_file(const char *filepath) {
    FILE *f = fopen(filepath, "r");

    if (f == NULL) {
        perror(filepath);
        return NULL;
    }

    char *contents = NULL;
    size_t sz = 0;

    do {
        contents = realloc(contents, ++sz);
        contents[sz-1] = fgetc(f);
    } while(!feof(f));

    contents[sz-1] = 0;

    printf("File loaded successfully: '%s'\n", filepath);

    return contents;
}

#include <signal.h>
void fpe_handle(int _sig) {
    (void)_sig;
    // NOTE: if this happens, we need to introduce a check into one of the words
    // that involves division
    error("Uncaught FPE\n");
    exit(1);
}


int main(int argc, char **argv) {
    signal(SIGFPE, fpe_handle);
    //wspacealloc();
    init_dict();
    if (argc > 1) {
        for (int i = 1; i < argc; i++) {
            char *f = load_file(argv[i]);

            if (f == NULL) continue;

	    line = 0;
            pointer = expression = f;

            eval();

            free(f);
            pointer = expression = NULL;
        }
    }
    repl();
    bye(0);
}

