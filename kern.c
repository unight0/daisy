#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include <unistd.h> //access()
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>

typedef void (*SysWord)(void);
typedef struct Word Word;
typedef intptr_t isize_t;
typedef uintptr_t usize_t;

typedef
#if INTPTR_MAX == INT64_MAX
double
#elif INTPTR_MAX == INT32_MAX
float
#else
#warning "You are running some esoteric shit, be careful!"
float
#endif
float_t;

typedef union {
    isize_t i;
    usize_t u;
    float_t f;
    char *s;
    void *p;
    Word *w;
} Cell;

struct Word {
    char flags;
    #ifdef FT_DOCS
    // Documentation
    const char *docstring;
    #endif
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

#ifdef FT_DOCS
#define SYSWORD(s,fl,dc) (Word){FL_PREDEFINED|(fl),(dc),.sw=(s)}
#else
#define SYSWORD(s,fl,dc) (Word){FL_PREDEFINED|(fl),.sw=(s)}
#endif /* FT_DOCS */

// A phony word for compiling numbers
void lit_w() {}
// Same but for strings
void strlit_w() {}
// Indicates the end of the word
void return_w() {}
// Not phony! But doesn't do anything on its own, serves as a marker
void does_w() {}

// Set in init_dict()
Word *lit_word = NULL;
Word *strlit_word = NULL;
Word *return_word = NULL;

#define STACK_SIZE 128
// Stack && topmost element
Cell stack[STACK_SIZE], *stacktop = stack;

// Name of the currently compiled word
char *comptarget = NULL;

// Dictionary entries point to the wordspace areas (apart from predefined words)
// TODO: change to a linked list and store in wordspace
// The last element in the dictionary is the first in the linked list
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
#define WSBLOCK_SIZE (size_t)16*1024
char wordspace[WSBLOCK_SIZE] = {0}, *wspacend = wordspace;

// These are the files that have already been loaded
// Avoid word redifinition
#define LOADED_COUNT 128
char *loaded[LOADED_COUNT] = {NULL};


// For error display
int repl_expression = 0;
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
char lastword[16] = {0};

size_t line = 0;
char *line_begin, *prev_line_begin = NULL;
// Display err instead of ok
int error_happened = 0;
// Error reporitng
#define error(cs, ...){                                                 \
        error_happened = 1;                                             \
        fprintf(stderr, "\033[m\033[1m--> At %lu, '%s': ", line+1, lastword); \
        fprintf(stderr, cs, ##__VA_ARGS__);                             \
        fprintf(stderr, "\033[m");                                      \
}

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
    for (char **ld = loaded; *ld != NULL; ld++) {
        printf("Freeing '%s'\n", *ld);
        free(*ld);
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

    //NOTE: copied the entirety of find_word() here because we don't want the
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

// Pushes the beginning of the wordspace
void base_w() {
    if (askspace(1)) return;
    (++stacktop)->p = (void*)wordspace;
}

// Pushes the maximum available address in the wordspace
void tip_w() {
    if (askspace(1)) return;
    (++stacktop)->p = (void*)(wordspace+WSBLOCK_SIZE);
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

    if (wspacend < wordspace) {
        error("Bottom break: wspacend < wordspace.\n"
              "Too many bytes have been unreserved.\n"
              "Resetting memory...\n");
        wspacend = wordspace;
    }

    if (wspacend >= wordspace+WSBLOCK_SIZE) {
        error("Top break: wspacend > wordspace+WSBLOCK_SIZE\n"
              "Too many bytes have been reserved\n"
              "Resetting memory...\n");
        wspacend = wordspace;
    }
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

    if (ftruncate(fd, sz)) {
        error("Couldn't truncate(resize) file: ");
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

// Read from file
// FD BUF LEN -- FD BUF
void read_w() {
    if (ASKSTACK(3)) return;

    size_t len = stacktop--->u;
    
    void *buf = stacktop--->p;
    
    int fd = stacktop->i;


    if(read(fd, &buf, len) == -1) {
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

void rename_w() {
    ASKFILENAME(to);
    ASKFILENAME(from);
    
    if(rename(from, to)) {
        error("Couldn't rename file '%s' to '%s'\n", from, to);
        perror("");
    }
}

void rmfile_w() {
    ASKFILENAME(filename);
    
    if(remove(filename)) {
        error("Couldn't delete file '%s'\n", filename);
        perror("");
    }
}

// TODO: replace by ACCESS (?)
void filee_w() {
    ASKFILENAME(filename);

    // Returns 0 if exists, returns -1 if doesn't
    (++stacktop)->u = !access(filename, F_OK);
}

void dumpstack_w() {
    printf("\nSTACK DUMP\n");
    for (Cell *p = stacktop; p > stack; p--) {
        printf("%ld : %p", p->i, p->p);
        if (p->p == wspacend) printf("=HERE");
        putchar('\n');
    }
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

// Ugly, but there's no other way
int syscall_disp(long sysc, long argnum, long arg[argnum]) {
    if (argnum == 0) return syscall(sysc);
    if (argnum == 1) return syscall(sysc, arg[0]);
    if (argnum == 2) return syscall(sysc, arg[0], arg[1]);
    if (argnum == 3) return syscall(sysc, arg[0], arg[1], arg[2]);
    if (argnum == 4) return syscall(sysc, arg[0], arg[1], arg[2], arg[3]);
    if (argnum == 5) return syscall(sysc, arg[0], arg[1], arg[2], arg[3], arg[4]);
    if (argnum == 6) return syscall(sysc, arg[0], arg[1], arg[2], arg[3], arg[4], arg[5]);
    error("SYSCALL with more than 6 args is unsupported.\n"
          "Note: wtf are you trying to do?\n");
    return -1;
}

void syscall_w() {
    if (ASKSTACK(2)) return;

    long sysc = (long)stacktop--->i;
    size_t argnum = stacktop--->i;

    long args[argnum];
    memset(args, 0, argnum*sizeof(long));

    for (size_t i = 0; i < argnum; i++) {
        if (ASKSTACK(1)) return;
        args[i] = stacktop--->i;
    }
    
    int ret = syscall_disp(sysc, argnum, args);
    if (ret == -1)
        (++stacktop)->i = errno;
    else
        (++stacktop)->i = 0;
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

void putflags(Word *w) {
    if (!w->flags) {
        printf("No flags\n");
        return;
    }

    //printf("'%s' = %d:\n", word, w->flags);

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

    putflags(w);
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

            /* TODO: replace this later (?) */
            if (p->w.flags & FL_PREDEFINED) {
                error("Attempting to redefine a predefined word\n");
                return;
            }

            // Reassign
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
    
    error("Unknown word '%s'.\n", word);
    return NULL;
}

//TODO: DRY it up a bit
Word *find_wordq(char *word) {
    // (convert to uppercase)
    for (char *p = word; *p; ++p) *p = toupper(*p);
    // Search dictionary entries
    for(DictEntry *p = dict; p < dicttop; ++p) {
        //printf("%s =? %s\n", word, p->name);
        if(!strcmp(p->name, word)) return &(p->w);
    }
    
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

void create_w() {
    char *name = parse();

    if (name == NULL) {
        error("Error: No word provided for 'create'\n");
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

    
    ((Cell*)wspacend)->w = lit_word;
    wspacend += sizeof(Cell);

    Cell *data = (Cell*)wspacend;
    wspacend += sizeof(Cell);
    
    ((Cell*)wspacend)->w = return_word;
    wspacend += sizeof(Cell);

    data->p = (void*)wspacend;
}



size_t bodylen(Cell*);
void scolon_w() {
    Word *w = find_word(comptarget);

    comptarget = NULL;
    
    if (w == NULL) {
        error("Cannot assign a body to a non-existent word!\n");
        comptarget = NULL;
    }

    *(Cell*)wspacend = (Cell){.w=return_word};
    wspacend += sizeof(Cell);

    //TODO: why tf did I put it here? Does it do smth? Investigate.
    //INVESTIGATION(1): so far it just breaks stuff or doesn't do anything, commenting out for now
    //wspacend++;


    if (error_happened) {
        error("A compilation error happened -- abandoning the word\n");
        //FIXME!!!: if we're redifining, this will break stuff
        dicttop--;
    }
    
    // Back into interpretation mode
    state = 0;
}

void execute_raw(Cell *begin);
void execute_w() {
    if(ASKSTACK(1)) return;

    //TODO!!!!!!!
    //Cell *end = (Cell*)stacktop--->p;
    //(void)end;
    Cell *begin = (Cell*)stacktop--->p;

    execute_raw(begin);
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


// Identifies the actual file using the filehint
// Filehints can look like:
// <filename> (will attempt to look in '.' and in INCLUDE_PATH)
// "<exact-filename>"
char *find_by_filehint(char *filehint) {
    // <exact-filename>
    if (*filehint == '"' && filehint[strlen(filehint)-1] == '"') {
        filehint[strlen(filehint)-1] = 0;
        filehint++;
        return filehint;
    }

    // Local directory
    if (access(filehint, F_OK) == 0) {
        return filehint;
    }
    
    // Tip: Define INCLUDE_PATH from the outside
#ifndef INCLUDE_PATH
    return NULL;
#else
    // Should be enough
    static char path[1024] = {0};
    strncpy(path, INCLUDE_PATH, 1024);

    // Append the '/' at the end
    if (path[strlen(path)-1] != '/') {
    path[strlen(path)] = '/';
    }

    strncat(path, filehint, 1024-strlen(path));
    
    // Cannot find file
    if (access(path, F_OK)) {
    return NULL;
    }
    return path;
#endif
}



void eval_file(const char *filename);
// Loads file
void load_w() {
    char *filehint = parse();
    
    char *filename = find_by_filehint(filehint);

    if (filename == NULL) {
        error("Could not find file using filehint '%s'!\n", filehint);
        return;
    }

    // Note: if you load more than 128 files, this will silently
    // load the files that have already been loaded
    char **ld = loaded;
    for (; *ld != NULL && (ld-loaded) < LOADED_COUNT; ld++) {
        if (!strcmp(*ld, filename)) {
            printf("Not loading already loaded file '%s'\n", filename);
            return;
        }
    }
    
    //printf("Located file for loading: '%s'\n", filename);

    char *str = malloc(strlen(filename)+1);
    strcpy(str, filename);

    ld++;
    // Just silently abort
    if (ld-loaded < LOADED_COUNT)
        *ld = filename;
    else free(filename);

    eval_file(filename);
}

// DRY!
#define DOCGUARD(c) for(int i = 0; i < c; i++) fputc('-', stdout); fputc('\n', stdout);
#ifdef FT_DOCS
// Shows docstring for the word
void doc_w() {
    char *word = parse();

    if (word == NULL) {
        error("No word provided for DOC!\n");
        return;
    }

    Word *w = find_word(word);

    if (w == NULL) {
        error("Cannot show documentation for a non-existent word!\n");
        return;
    }
    
    DOCGUARD(80);
    
    if (w->docstring == NULL) {
        printf("No documentation available\n");
        DOCGUARD(80);
        return;
    }

    if (!*w->docstring) {
        printf("Documentation is empty\n");
        DOCGUARD(80);
        return;
    }

    printf("DOCUMENTATION FOR '%s'\n", word);

    putflags(w);
    
    printf("\n%s", w->docstring);
    
    DOCGUARD(80);
}

void docmem_w() {
    DOCGUARD(80);

    printf("Daisy FORTH kernel is similar to its memory management to other FORTHs.\n");
    printf("There is one large preallocated fixed-size(will be made extensible \nin the future updates) memory space.\n");
    printf("This memory space is called wordspace.\n");
    printf("The kernel uses it automatically to store newly-defined words.\n");
    printf("However, when the user needs to allocate memory, the same wordspace is used.\n");
    printf("Therefore be careful with anything that involves RESERVING memory\nwhile compiling a word because it may write your data into the body of the word!\n");
    printf("===WORDSPACE===\n");
    printf("+----------------------------------------------------------+\n");
    printf("|WORD1|WORD2|USER DATA|WORD3|USER DATA|                    |\n");
    printf("+----------------------------------------------------------+\n");
    printf(" ^                                     ^                  ^\n");
    printf(" BASE                                  HERE             TIP\n");
    
    DOCGUARD(80);
}

#endif /* FT_DOCS */

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

// ... B C A --
void dropall_w(void) {
    stacktop = stack;
}

// A -- AA
void dup_w(void) {
    if(ASKSTACK(1)) return;
    if(askspace(1)) return;

    Cell a = *stacktop;

    *(++stacktop) = a;
}

// A B -- A B A
void over_w(void) {
    if(ASKSTACK(2)) return;
    if(askspace(1)) return;

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

void pick_w(void) {
    if(ASKSTACK(1)) return;

    Cell n = *stacktop--;

    if(ASKSTACK(n.u)) return;

    if (stacktop-n.u < stack) {
        error("PICK: too deep!\n");
        return;
    }

    stacktop++;
    *stacktop = *(stacktop-n.u);
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
        printf("\033[31;1merr\033[m\n");
        return;
    }
    const size_t stack_sz = (stacktop-stack);

    char *cm = "";

    if (state) cm = "(c)";

    if (stack_sz)
        printf("\033[32;1m%sok %lu\033[m\n", cm, stack_sz);
    else
        printf("\033[32;1m%sok\033[m\n", cm);
}

char *parse() {
    if (pointer == NULL) return NULL;
    
    // End of line
    if (!*pointer) return NULL;

    // Skip spaces
    for(;isspace(*pointer);++pointer) if (*pointer == '\n') {
        prev_line_begin = line_begin;
        line_begin = pointer;

        line++;
    }
    
    // Remember last word processed for errors
    char *word = pointer;
    strncpy(lastword, pointer, 16);
    // IDK why
    for (char *p = lastword; *p; p++)
        if (isspace(*p)) { *p = 0; break; }

    int escape = 0;
    // A string
    if (*pointer == '"') {
    pointer++;
    // Skip until closing '"'
    // NOTE: only anknowledges character escaping, doesn't convert them to the corresponding character
    for(;(*pointer != '"'||escape) && *pointer;++pointer) {
        if (escape) escape = 0;
        if (*pointer == '\\') escape = 1;
    }
    pointer++;
    }
    // Anything else
    else {
    // Skip until next space
    for(;!isspace(*pointer) && *pointer;++pointer);
    if (*pointer == '\n') {
        prev_line_begin = line_begin;
        line_begin = pointer;
        line++;
    }
    }

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
// 4 -- string
char identify(char* word) {
    int dotcount, signcount, cc;
    dotcount = signcount = cc = 0;

    // Char looks like 'c'
    if (strlen(word) == 3 && *word == '\'' && word[2] == '\'') {
        return 3;
    }

    // String looks like "string" (spaces are supported through parse())
    if (*word == '"' && word[strlen(word)-1] == '"') {
    return 4;
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
    if (signcount > 1 || signcount == cc) return 0;
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

char *load_string(char *str);
void compile_string(char *str) {
    // STRLIT
    *(Cell*)wspacend = (Cell){.w=strlit_word};
    wspacend += sizeof(Cell);

    load_string(str);
}

// Execute a thing from a userword
void execute(Word *w);
Cell *execute_thing(Cell *cp) {
    Cell c = *cp;

    // System word
    if (c.w->flags & FL_PREDEFINED) {
        // A phony pointer signals that the next Cell is data
        if (c.w->sw == lit_w) {
            ++stacktop;
            *stacktop = *(cp+1);
            return cp+2;
        }
        if (c.w->sw == strlit_w) {
            ++stacktop;
            stacktop->p = (void*)(cp+1);
            char *ch;
            for(ch = (char*)(cp+1); *ch; ch++);
            return (Cell*)(ch+1);
        }
        else c.w->sw();
        if (need_jump) {
            need_jump = 0;
            /*if (absjump < begin || absjump > end) {
                error("Jump outside of body: %p\n", (void*)absjump);
                return cp+1;
                }*/

            return absjump;
        }
        return cp+1;
    }

    // Userword
    // Note: recursive
    // TODO: rewrite non-recursively (?)
    execute(c.w);

    //printf("}\n");
    return cp+1;
}

size_t bodylen(Cell *c) {
    size_t sz = 0;
    int last_lit = 0;
    for (;last_lit||c->w->sw != return_w; c++) {
        // This is needed to avoid nonexistent pointer dereference
        if (last_lit) last_lit = 0;
        if (c->w == lit_word) last_lit = 1;
        sz += 1;
    }
    // Account for RETURN
    return sz+1;
}

// Note: we abandon 3-cell definition per does>
// LIT <ptr> RETURN
// TODO: use BRANCH instead of copying
// + reserve space enough for adding BRANCH when CREATEing
// Thus, we will not be abandoning definitions
void dodoes(Cell *c) {
    Cell *oldbdy = (dicttop-1)->w.body;

    if ((dicttop-1)->w.flags & FL_PREDEFINED) {
        error("Cannot modify a predefined word with does>!\n"
              "Create a new word with CREATE <word> before using does>!\n");
        return;
    }

    (dicttop-1)->w.body = (Cell*)wspacend;

    Cell *body = (Cell*)wspacend;

    // LIT 
    body->w = lit_word;
    body++;

    // Pointer
    *body = *(oldbdy+1);
    body++;

    // Copy the rest
    memcpy(body, c, (bodylen(c))*sizeof(Cell));

    wspacend = (char*)(body+bodylen(c));
}

void execute_raw(Cell *begin) {
    for (Cell *t = begin;;) {
        if (error_happened) {
            error("An error occured during execution -- stopping\n");
            break;
        }

        if (t->w->sw == return_w)
            break;
        
        if (t->w->sw == does_w) {
            //(dicttop-1)->w.body = t+1;
            dodoes(t+1);
            break;
        }
        
        t = execute_thing(t);
    }
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

    execute_raw(w->body);

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

char *load_string(char *str) {
    char *begin = wspacend;

    int escape = 0;
    for (char *p = str; *p && p < (str+strlen(str)); ++p) {
        if (*p == '\\' && !escape) {
            escape = 1;
            continue;
        }
        
        if (!escape) {
            *wspacend++ = *p;
            continue;
        }
    
        // Escaped characters below
        escape = 0;

        if (*p == '\\') {
            *wspacend++ = '\\';
        }
        else if (*p == 'n') {
            *wspacend++ = '\n';
        }
        else if (*p == 't') {
            *wspacend++ = '\t';
        }
        else if (*p == '"') {
            *wspacend++ = '"';
        }
    }
    // Finish the c-string
    *wspacend++ = 0;

    return begin;
}

void eval() {
    for (;;) {
       if (error_happened) {
           error("Aborting evaluation\n");
           break;
       }
       
       char *word = parse();

       // EOL
       if (word == NULL) break;
       if (*word == 0) break;

       // Identify type
       char type = identify(word);

       // String
       if (type == 4) {
           // Cut the '"' off
           word++;
           word[strlen(word)-1] = 0;
           
           if (state) {
               compile_string(word);
               continue;
           }

           (++stacktop)->p = load_string(word);

           continue;
       }
       
       // Numbers are just pushed to stack
       if (type) {
           if (state) compile_number(word, type);
           else push_number(word, type);
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
void decompile(Cell *begin) {
    int last_lit = 0;
    
    for (Cell *p = begin; last_lit || p->w->sw != return_w; p++) {
        const char *name = reverse_search(p->w);
        
        // TODO: add strlit support
        if (last_lit) {
            last_lit = 0;
            printf("%lu ", p->i);
        }

        else if (name == NULL) printf("<?(%p)> ", p->p);

        else {
            if (p->w == lit_word) last_lit = 1;
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

    printf("Note: SEE cannot properly handle RETURNs in the middle of a word (yet).\n");

    putflags(w);

    if (w->flags & FL_PREDEFINED) {
        printf("<asm>\n");
        return;
    }

    decompile(w->body);
}

#ifdef FT_DOCS
#include "docs.c"
#endif

// Dictionary initialization
void init_dict() {
    /* Basics */
    *dicttop++ = (DictEntry){"EXIT", SYSWORD(exit_w,0,doc_exit)};
    *dicttop++ = (DictEntry){"LIT", SYSWORD(lit_w,FL_IMMEDIATE,doc_lit)};
    lit_word = &(dicttop-1)->w;
    *dicttop++ = (DictEntry){"STRLIT", SYSWORD(strlit_w,FL_IMMEDIATE,doc_strlit)};
    strlit_word = &(dicttop-1)->w;
    *dicttop++ = (DictEntry){"RETURN", SYSWORD(return_w,0,doc_return)};
    return_word = &(dicttop-1)->w;
    *dicttop++ = (DictEntry){"BRANCH", SYSWORD(branch_w,FL_COMPONLY,doc_branch)};
    *dicttop++ = (DictEntry){"0BRANCH", SYSWORD(zbranch_w,FL_COMPONLY,doc_zbranch)};
    *dicttop++ = (DictEntry){"'", SYSWORD(findword_w,0,doc_findword)};
    *dicttop++ = (DictEntry){":", SYSWORD(colon_w,FL_IMMEDIATE|FL_INTERONLY,doc_colon)};
    *dicttop++ = (DictEntry){"CREATE", SYSWORD(create_w,0,doc_create)};
    *dicttop++ = (DictEntry){"DOES>", SYSWORD(does_w,0,doc_does)};
    *dicttop++ = (DictEntry){";", SYSWORD(scolon_w,FL_IMMEDIATE|FL_COMPONLY,doc_semicolon)};
    *dicttop++ = (DictEntry){"EXECUTE", SYSWORD(execute_w,0,doc_execute)};
    *dicttop++ = (DictEntry){"IMMEDIATE", SYSWORD(immediate_w,FL_IMMEDIATE|FL_COMPONLY,doc_immediate)};
    *dicttop++ = (DictEntry){"INTERPRETATION-ONLY", SYSWORD(interpretation_only_w,FL_IMMEDIATE|FL_COMPONLY,doc_interonly)};
    *dicttop++ = (DictEntry){"COMPILE-ONLY", SYSWORD(compile_only_w,FL_IMMEDIATE|FL_COMPONLY,doc_componly)};
    *dicttop++ = (DictEntry){"LOAD", SYSWORD(load_w,FL_INTERONLY,doc_load)};
    *dicttop++ = (DictEntry){"DUMPSTACK", SYSWORD(dumpstack_w,0,"")};
    *dicttop++ = (DictEntry){"SYSCALL", SYSWORD(syscall_w,0,"")};
    
#ifdef FT_DOCS
    *dicttop++ = (DictEntry){"DOC", SYSWORD(doc_w,FL_INTERONLY,doc_doc)};
    *dicttop++ = (DictEntry){"DOC-MEM", SYSWORD(docmem_w,FL_INTERONLY,doc_docmem)};
#endif


    /* Memory */
    //TODO: documentation + doc-mem, because the memory system of forth
    //may be confusing at the beginning.
    //Moreover, allow strings to be LIT'd (or make up LITSTR), because we want
    //to be able to do this:
    //: hi "hello, world!" type ;
    *dicttop++ = (DictEntry){"STATE", SYSWORD(state_w,0,doc_state)};
    *dicttop++ = (DictEntry){"@B", SYSWORD(bfetch_w,0,doc_bfetch)};
    *dicttop++ = (DictEntry){"!B", SYSWORD(bwr_w,0,doc_bwr)};
    *dicttop++ = (DictEntry){"@", SYSWORD(fetch_w,0,doc_fetch)};
    *dicttop++ = (DictEntry){"!", SYSWORD(wr_w,0,doc_wr)};
    *dicttop++ = (DictEntry){"HERE", SYSWORD(here_w,0,doc_here)};
    *dicttop++ = (DictEntry){"BASE", SYSWORD(base_w,0,doc_base)};
    *dicttop++ = (DictEntry){"TIP", SYSWORD(tip_w,0,doc_tip)};
    *dicttop++ = (DictEntry){"RESERVE", SYSWORD(reserve_w,0,doc_reserve)};
    *dicttop++ = (DictEntry){"CELLS", SYSWORD(cells_w,0,doc_cells)};

    /* File management */
    ///dicttop++ = (DictEntry){"READFILE", SYSWORD(readfile_w,0,NULL)};
    //   #ifdef EXTENSION_FILESYS
    *dicttop++ = (DictEntry){"OPEN", SYSWORD(open_w,0,doc_open)};
    *dicttop++ = (DictEntry){"TOUCH", SYSWORD(touch_w,0,doc_touch)};
    *dicttop++ = (DictEntry){"TRUNC", SYSWORD(trunc_w,0,doc_trunc)};
    //*dicttop++ = (DictEntry){"WRITE", SYSWORD(write_w,0,doc_write)};
    //*dicttop++ = (DictEntry){"READ", SYSWORD(read_w,0,doc_read)};
    *dicttop++ = (DictEntry){"SEEK", SYSWORD(seek_w,0,doc_seek)};
    *dicttop++ = (DictEntry){"FILESIZE", SYSWORD(filesize_w,0,doc_fsize)};
    *dicttop++ = (DictEntry){"CLOSE", SYSWORD(close_w,0,doc_close)};
    *dicttop++ = (DictEntry){"FILE-EXISTS?", SYSWORD(filee_w,0,doc_filex)};
    *dicttop++ = (DictEntry){"RENAME", SYSWORD(rename_w,0,doc_rename)};
    *dicttop++ = (DictEntry){"RMFILE", SYSWORD(rmfile_w,0,doc_rmfile)};
    //#endif

    // Very efficient, but introduces its own problems that overpower
    // the benefits
    //*dicttop++ = (DictEntry){"MAPFILE", SYSWORD(mapfile_w,0,NULL)};
    //*dicttop++ = (DictEntry){"UNMAPFILE", SYSWORD(unmapfile_w,0,NULL)};

    
    /* Reading from expression itself */
    // Receives 1 next byte from expression
    *dicttop++ = (DictEntry){"EB", SYSWORD(exprbyte_w,0,doc_eb)};

    /* Development */
    *dicttop++ = (DictEntry){"WORDS", SYSWORD(words_w,0,doc_words)};
    *dicttop++ = (DictEntry){"FLAGS", SYSWORD(flags_w,0,doc_flags)};
    *dicttop++ = (DictEntry){"SEE", SYSWORD(see_w,0,doc_see)};

    /* Stack manipulation */
    // TODO: implement PICK
    *dicttop++ = (DictEntry){"SWAP", SYSWORD(swap_w,0,doc_swap)};
    *dicttop++ = (DictEntry){"DROP", SYSWORD(drop_w,0,doc_drop)};
    *dicttop++ = (DictEntry){"DROPALL", SYSWORD(dropall_w,0,doc_dropall)};
    *dicttop++ = (DictEntry){"DUP", SYSWORD(dup_w,0,doc_dup)};
    *dicttop++ = (DictEntry){"OVER", SYSWORD(over_w,0,doc_over)};
    *dicttop++ = (DictEntry){"ROT", SYSWORD(rot_w,0,doc_rot)};
    *dicttop++ = (DictEntry){"PICK", SYSWORD(pick_w,0,doc_pick)};

    /* I/O */
    //TODO: emit and . are not primitive enough;
    //Introduce read() and write() and file descriptors and the build . and
    //emit on top of that
    //NOTE: read() and write() are cool and all, but it will limit the
    //crossplatformity of the system. But do we even care? That's the question.
    *dicttop++ = (DictEntry){".", SYSWORD(putint_w,0,doc_dot)};
    *dicttop++ = (DictEntry){".F", SYSWORD(putflt_w,0,doc_dotf)};
    //*dicttop++ = (DictEntry){"EMIT", SYSWORD(emit_w,0,doc_emit)};


    /* Logic */
    //TODO: bitwise operations
    *dicttop++ = (DictEntry){"AND", SYSWORD(and_w,0,doc_arith)};
    *dicttop++ = (DictEntry){"OR", SYSWORD(or_w,0,doc_arith)};
    *dicttop++ = (DictEntry){"NOT", SYSWORD(not_w,0,doc_arith)};
    *dicttop++ = (DictEntry){"XOR", SYSWORD(xor_w,0,doc_arith)};

    /* Arithmetics */
    *dicttop++ = (DictEntry){"=", SYSWORD(equ_w,0,doc_arith)};
    *dicttop++ = (DictEntry){">", SYSWORD(gre_w,0,doc_arith)};
    *dicttop++ = (DictEntry){"<", SYSWORD(les_w,0,doc_arith)};
    *dicttop++ = (DictEntry){"%", SYSWORD(mod_w,0,doc_arith)};
    *dicttop++ = (DictEntry){"+", SYSWORD(sum_w,0,doc_arith)};
    *dicttop++ = (DictEntry){"-", SYSWORD(sub_w,0,doc_arith)};
    *dicttop++ = (DictEntry){"/", SYSWORD(div_w,0,doc_arith)};
    *dicttop++ = (DictEntry){"*", SYSWORD(mul_w,0,doc_arith)};
    *dicttop++ = (DictEntry){"+F", SYSWORD(fsum_w,0,doc_arithf)};
    *dicttop++ = (DictEntry){"-F", SYSWORD(fsub_w,0,doc_arithf)};
    *dicttop++ = (DictEntry){"/F", SYSWORD(fdiv_w,0,doc_arithf)};
    *dicttop++ = (DictEntry){"*F", SYSWORD(fmul_w,0,doc_arithf)};

    userwords = dicttop;
}

#define VERSION "0.6"

//#include <readline/readline.h>
//#include <readline/history.h>

void greet() {
    
    printf("Daisy forth kernel v"VERSION"\n");
    printf("Memory %ldKB\n", WSBLOCK_SIZE/1024);
    /*DOCGUARD(80);

    printf("Type in `words` to view available words.\n");
    printf("Type in `doc <word>` to access the available documentation about the word.\n");
    printf("Type in `load basis.fs` to load the standard library.\n");
    printf("Press ^C or type in `0 exit` to exit with code 0.\n");

    DOCGUARD(80);*/
}

void repl() {
   char *line = NULL;
   //size_t line_sz = 0;

   // Greet user
   greet();
   
   // Loop
   for (;;) {
       // Read
       //printf("\033[3m");
       size_t line_sz = 0;
       getline(&line, &line_sz, stdin);
       //line = readline(NULL);

       if (line == NULL) continue;
       
       //if (*line) add_history(line);
       
       //printf("\033[m");
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
       repl_expression = 1;
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

void eval_file(const char *filename) {
    char *f = load_file(filename);
    
    if (f == NULL) return;

    /* Save previous state */
    int sline = line;
    char *splb = prev_line_begin, *slb = line_begin, *sp = pointer, *se = expression;
    
    line = 0;
    prev_line_begin = line_begin = pointer = expression = f;
    
    eval();

    free(f);
    /* Restore state */
    //prev_line_begin = line_begin = pointer = expression = NULL;
    line = sline, prev_line_begin = splb, line_begin = slb, pointer = sp, expression = se;
}

#include <signal.h>
void fpe_handle(int _sig) {
    (void)_sig;
    // NOTE: if this happens, we need to introduce a check into one of the words
    // that involves division
    error("uncaught FPE\n");
    exit(1);
}

void segv_handle(int _sig) {
    (void)_sig;

    error("Segmentation fault!\n");
    printf("You poked and peeked where you shouldn't have.\n");
    printf("Restoring system...\n");
    printf("Warning: stability is not guaranteed!\n");
    error_happened = 0;
    repl();
}


int main(int argc, char **argv) {
    signal(SIGFPE, fpe_handle);

    struct sigaction sa;
    sa.sa_handler = segv_handle;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_NODEFER;
    sigaction(SIGSEGV, &sa, NULL);
    
    //wspacealloc();
    
    init_dict();

    if (argc > 1) {
        for (int i = 1; i < argc; i++) {
            eval_file(argv[i]);
        }
    }
    
    repl();
    bye(0);
}

