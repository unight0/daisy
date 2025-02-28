#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>

typedef void (*SysWord)(void);
typedef struct Word Word;
typedef union {
    int64_t i;
    uint64_t u;
    double f;
    void *p;
    Word *w;
} Cell;

struct Word {
    char flags;
    size_t size;
    //char signature;
    union {
        Cell *body;
        SysWord sw;
    };
};

enum {
    TH_CELL = (1<<0),
    TH_WORD = (1<<1)
};

enum {
    FL_IMMEDIATE  = (1<<0),
    FL_COMPONLY   = (1<<1),
    FL_INTERONLY  = (1<<2),
    FL_PREDEFINED = (1<<3),
};

#define SYSWORD(s,fl) (Word){FL_PREDEFINED|(fl),0,.sw=(s)}

// Let's hope the system won't give us that address when we malloc() and that
// NULL != LIT_PTR
// TODO: make it 100% safe
#define LIT_PTR (SysWord)0x100
Word *lit_word = NULL;

#define STACK_SIZE 128
// Stack && topmost element
Cell stack[STACK_SIZE], *stackptr = stack;

char *comptarget = NULL;
size_t comptarget_sz = 0;

// Dictionary entries point to the wordspace areas
#define DICT_SIZE 128 
typedef struct { char *name; Word w; } DictEntry;
DictEntry dict[DICT_SIZE] = {0}, *dictptr = dict, *userwords = dict;


// Memory space for words. Nonreleasable allocation for user data is allowed
// too.
// TODO: malloc it
#define WSPACE_SIZE sizeof(Word)*512
char wordspace[WSPACE_SIZE] = {0}, *wspaceptr = wordspace;


// Expression that is processed at the moment
char *expression = NULL;
char *pointer = NULL;

// 0 = interpreting
// 1 = compiling
char state = 0;
// Absolute jump for branch and 0branch
// NOTE(2): this is basically timetravel
int need_jump = 0;
Cell *absjump = 0;

int error_happened = 0;
// Error reporitng
#define error(cs, ...){\
    error_happened = 1;\
    fprintf(stderr, "\033[m\033[1mAt %lu: ", pointer-expression);\
    fprintf(stderr, cs, ##__VA_ARGS__); \
    fprintf(stderr, "\033[m");}\

// Report insufficient arguments
void ins_arguments(const char *who, size_t req, size_t got) {
    error("In %s: expected %lu got %lu\n", who, req, got);
}

// Ask to have NUM arguments on stack
int askstack(const char *who, size_t num) {
    if ((size_t)(stackptr-stack) < num) {
        ins_arguments(who, num, (stackptr-stack));
        return 1;
    }
    return 0;
}

#define ASKSTACK(num) askstack(__func__, num)

// Ask to have NUM space left on stack arguments on stack
int askspace(size_t num) {
    const size_t current_size = (stackptr-stack);
    const size_t leftover = STACK_SIZE - current_size;

    if (leftover < num) {
        error("Error: stack overflow\n");
        return 1;
    }

    return 0;
}

/* Begin predefined words *******/

// Note: also called from main()
void bye_w() {
    for (DictEntry *p = userwords; p < dictptr; p++) {
        free(p->name);
    }
    exit(0);
}

// N -- 
void branch_w() {
    if (ASKSTACK(1)) return;

    Cell n = *stackptr--;

    need_jump = 1;
    absjump = (Cell*)n.p;
}

// COND N --
// Branches when COND == 0
void zbranch_w() {
    if (ASKSTACK(1)) return;

    Cell n    = *stackptr--;
    Cell cond = *stackptr--;

    if(cond.i) return;

    need_jump = 1;
    absjump = (Cell*)n.p;
}

// Wrapper for find_word()
char *parse(void);
Word *find_word(char*);
void findword_w() {
    if(askspace(1)) return;

    char *word = parse();

    if (word == NULL || !*word) {
        error("No name provided for FINDWORD\n");
        return;
    }

    Word *w = NULL;

    //NOTE: copied the entirety of find_word() to here because we don't want the
    //error message to pop up here. TODO: Is this needed?
    // Uppercase
    for (char *p = word; *p; ++p) *p = toupper(*p);

    // Search
    for(DictEntry *p = dict; p < dictptr; ++p) {
        if(!strcmp(p->name, word)) { 
            w = &(p->w);
            break;
        }
    }

    (++stackptr)->p = (void*)w;
}

char *parse(void);
Word *find_word(char*);
void postpone_w() {
    if(askspace(1)) return;

    char *word = parse();

    if (word == NULL || !*word) {
        error("No name provided for FINDWORD\n");
        return;
    }

    Word *w = find_word(word);
    if (w == NULL) {
        error("Cannot postpone '%s': doesn't exist!\n", word);
        return;
    }

    // Compile the word
    //*(Cell*)wspaceptr = (Cell){TH_WORD,.w=w};
    *(Cell*)wspaceptr = (Cell){.w=w};
    wspaceptr += sizeof(Cell);
}

// Pushes pointer to the topmost wspace element
void here_w() {
    if (askspace(1)) return;
    
    (++stackptr)->p = (void*)wspaceptr;
}

// Reads one byte
void bfetch_w() {
    if (ASKSTACK(1)) return;
    if (askspace(1)) return;

    char *ptr = (char*)stackptr--->p;
    
    (++stackptr)->i = *ptr;
}

// Writes one byte
void bwr_w() {
    if (ASKSTACK(2)) return;

    char *addr = stackptr--->p;
    char byte = (char)stackptr--->i;
    
    *addr = byte;
}

// Reads one cell
void fetch_w() {
    if (ASKSTACK(1)) return;
    if (askspace(1)) return;

    Cell *ptr = (Cell*)stackptr--->p;
    
    *++stackptr = *ptr;
}

// Writes one cell
void wr_w() {
    if (ASKSTACK(2)) return;

    Cell *addr = stackptr--->p;
    Cell c = *stackptr--;
    
    *addr = c;
}

// Reserves n bytes in wordspace
// Strictly allocates, doesn't deallocate
void reserve_w() {
    if (ASKSTACK(1)) return;
    
    size_t bytes = stackptr--->u; 

    wspaceptr += bytes;
}

// Pushes the amount of bytes occupied by N cells
// N -- B
void cells_w() {
    if (ASKSTACK(1)) return;

    size_t cells = stackptr--->u; 

    (++stackptr)->u = cells*sizeof(Cell);
}

void putint_w() {
    if (ASKSTACK(1)) return;
    printf("%ld", stackptr--->i);
}
void putflt_w() {
    if(ASKSTACK(1)) return;
    printf("%f", stackptr--->f);
}
void emit_w() {
    if(ASKSTACK(1)) return;
    putchar(stackptr--->i);
}

void exprbyte_w() {
    if(askspace(1)) return;

    (++stackptr)->i = *pointer;
    
    // Advance pointer if not EOL
    if(*pointer) pointer++;
}

void words_w() {
    for(DictEntry *p = dict; p < dictptr; p++) {
        printf("%s ", p->name);
    }
    printf("(total %lu/%lu)\n", dictptr-dict, DICT_SIZE);
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

    if (!w->flags) {
        printf("'%s' = 0 --> no flags\n", word);
        return;
    }

    printf("'%s' = %lu:\n", word, w->flags);

    if (w->flags & FL_PREDEFINED)
        printf("PREDEFINED\n");
    if (w->flags & FL_IMMEDIATE)
        printf("IMMEDIATE\n");
    if (w->flags & FL_COMPONLY)
        printf("COMPILE-TIME ONLY\n");
    if (w->flags & FL_INTERONLY)
        printf("RUNTIME-ONLY\n");
}

//Word *newword() {
//}

// Adds entry if not present
void dict_entry(char *entry) {
    assert(entry != NULL);

    for (DictEntry *p = dict; p < dictptr; p++) {
        // Already present
        if (!strcmp(p->name, entry)) return;
    }


    // Add entry
    // NOTE: abandons previous word declaration, which leaks memory. However, we
    // don't redefine words often, hopefully...
    dictptr->name = entry;
    dictptr->w.body = (Cell*)wspaceptr;
    dictptr++;
}

Word *find_word(char *word) {
    // (convert to uppercase)
    for (char *p = word; *p; ++p) *p = toupper(*p);
    // Search dictionary entries
    for(DictEntry *p = dict; p < dictptr; ++p) {
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
    find_word(comptarget)->size = comptarget_sz;
    wspaceptr++;

    comptarget = NULL;
    comptarget_sz = 0;

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
void runtime_only_w() {
    find_word(comptarget)->flags |= FL_INTERONLY;
}

// A B -- B A
void swap_w(void) {
    if(ASKSTACK(2)) return;

    Cell b = *stackptr--;
    Cell a = *stackptr--;

    *(++stackptr) = b;
    *(++stackptr) = a;
}

// A --
void drop_w(void) {
    if(ASKSTACK(1)) return;
    --stackptr;
}

// A -- AA
void dup_w(void) {
    if(ASKSTACK(1)) return;

    Cell a = *stackptr;

    *(++stackptr) = a;
}

// A B -- A B A
void over_w(void) {
    if(ASKSTACK(2)) return;

    Cell b = *stackptr--;
    Cell a = *stackptr--;

    *(++stackptr) = a;
    *(++stackptr) = b;
    *(++stackptr) = a;
}

// A B C -- B C A
void rot_w(void) {
    if(ASKSTACK(3)) return;

    Cell c = *stackptr--;
    Cell b = *stackptr--;
    Cell a = *stackptr--;

    *(++stackptr) = b;
    *(++stackptr) = c;
    *(++stackptr) = a;
}

/* Boring repetative arithmetic definitions below */

// Macros make it less repetative
#define TWOOP(a,b,t,v) if(ASKSTACK(2))return;\
    Cell a = *stackptr--;                    \
    Cell b = *stackptr--;                    \
    (++stackptr)->t = (v);

// Should word for all data types
void equ_w(void) {
    TWOOP(b, a, i, a.i == b.i);
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
    TWOOP(b, a, i, a.i / b.i);
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
    TWOOP(b, a, f, a.f / b.f);
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

    Cell c = *stackptr--;

    (++stackptr)->i = !c.i;
}

/* End predefined words ********/
// Prompt
void ok(size_t line_sz) {
    if(error_happened) {
        error_happened = 0;
        return;
    }
    const size_t stack_sz = (stackptr-stack);

    if (stack_sz)
        printf("\033[1mok %lu\033[m\n", stack_sz);
    else
        printf("\033[1mok\033[m\n");
}

char *parse() {
    // End of line
    if (!*pointer) return NULL;

    // Skip spaces
    for(;isspace(*pointer);++pointer);

    char *word = pointer;

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
    *(Cell*)wspaceptr = (Cell){.w=w};
    wspaceptr += sizeof(Cell);
    comptarget_sz++;
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
    *(Cell*)wspaceptr = (Cell){.w=lit_word};
    wspaceptr += sizeof(Cell);

    // Number itself
    *(Cell*)wspaceptr = num;
    wspaceptr += sizeof(Cell);

    comptarget_sz += 2;
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
        *++stackptr = c;
        return;
    }

    // System word
    if (c.w->flags & FL_PREDEFINED) {
        // A phony pointer cat signals that the next Cell is cell
        if (c.w->sw == LIT_PTR) {
            is_cell = 1;
            return;
        }
        c.w->sw();
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
        w->sw();
        return;
    }

    // User words
    for (Cell *t = w->body; t < w->body+w->size || need_jump; t++) {
        if (need_jump) {
            if (absjump < w->body || absjump > w->body+w->size) {
                error("Jump outside of body: %p\n", absjump);
                break;
            }
            t = absjump;
            need_jump = 0;
        }

        execute_thing(*t);
    }
}


void push_number(char *word, char numericity) {
    assert(numericity);

    if (askspace(1)) exit(1);

    if (numericity == 1)
        (++stackptr)->i = atol(word);

    else if (numericity == 2)
        (++stackptr)->f = atof(word);

    else (++stackptr)->i = word[1];
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
        // Runtime-only
        if (w->flags & FL_INTERONLY) {
            error("Compiling runtime-only word\n");
            return 1;
        }
        return 0;
    }

    // Executing

    // Should not be executed
    if (w->flags & FL_COMPONLY) {
        error("Executing a compile-only word\n");
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


// Dictionary initialization
void init_dict() {
    /* Basics */
    *dictptr++ = (DictEntry){"BYE", SYSWORD(bye_w,0)};
    *dictptr++ = (DictEntry){"LIT", SYSWORD(LIT_PTR,0)};
    lit_word = &(dictptr-1)->w;
    *dictptr++ = (DictEntry){"BRANCH", SYSWORD(branch_w,FL_COMPONLY)};
    *dictptr++ = (DictEntry){"0BRANCH", SYSWORD(zbranch_w,FL_COMPONLY)};
    //*dictptr++ = (DictEntry){"FINDWORD", SYSWORD(findword_w,0)};
    *dictptr++ = (DictEntry){"POSTPONE", SYSWORD(postpone_w,FL_COMPONLY|FL_IMMEDIATE)};
    *dictptr++ = (DictEntry){":", SYSWORD(colon_w,FL_IMMEDIATE|FL_INTERONLY)};
    *dictptr++ = (DictEntry){";", SYSWORD(scolon_w,FL_IMMEDIATE|FL_COMPONLY)};
    *dictptr++ = (DictEntry){"IMMEDIATE", SYSWORD(immediate_w,FL_IMMEDIATE|FL_COMPONLY)};
    *dictptr++ = (DictEntry){"RUNTIME-ONLY", SYSWORD(runtime_only_w,FL_IMMEDIATE|FL_COMPONLY)};
    *dictptr++ = (DictEntry){"COMPILE-ONLY", SYSWORD(compile_only_w,FL_IMMEDIATE|FL_COMPONLY)};

    /* Memory */
    *dictptr++ = (DictEntry){"HERE", SYSWORD(here_w,0)};
    *dictptr++ = (DictEntry){"@B", SYSWORD(bfetch_w,0)};
    *dictptr++ = (DictEntry){"!B", SYSWORD(bwr_w,0)};
    *dictptr++ = (DictEntry){"@", SYSWORD(fetch_w,0)};
    *dictptr++ = (DictEntry){"!", SYSWORD(wr_w,0)};
    *dictptr++ = (DictEntry){"RESERVE", SYSWORD(reserve_w,0)};
    *dictptr++ = (DictEntry){"CELLS", SYSWORD(cells_w,0)};

    /* Reading from expression itself */
    // Receives 1 next byte from expression
    *dictptr++ = (DictEntry){"EB", SYSWORD(exprbyte_w,0)};

    /* Development */
    *dictptr++ = (DictEntry){"WORDS", SYSWORD(words_w,0)};
    *dictptr++ = (DictEntry){"FLAGS", SYSWORD(flags_w,0)};

    /* Stack manipulation */
    // TODO: implement PICK
    *dictptr++ = (DictEntry){"SWAP", SYSWORD(swap_w,0)};
    *dictptr++ = (DictEntry){"DROP", SYSWORD(drop_w,0)};
    *dictptr++ = (DictEntry){"DUP", SYSWORD(dup_w,0)};
    *dictptr++ = (DictEntry){"OVER", SYSWORD(over_w,0)};
    *dictptr++ = (DictEntry){"ROT", SYSWORD(rot_w,0)};

    /* I/O */
    //TODO: emit and . are not primitive enough;
    //Introduce read() and write() and file descriptors and the build . and
    //emit on top of that
    *dictptr++ = (DictEntry){".", SYSWORD(putint_w,0)};
    *dictptr++ = (DictEntry){".F", SYSWORD(putflt_w,0)};
    *dictptr++ = (DictEntry){"EMIT", SYSWORD(emit_w,0)};


    /* Logic */
    //TODO: bitwise operations
    *dictptr++ = (DictEntry){"AND", SYSWORD(and_w,0)};
    *dictptr++ = (DictEntry){"OR", SYSWORD(or_w,0)};
    *dictptr++ = (DictEntry){"NOT", SYSWORD(not_w,0)};
    *dictptr++ = (DictEntry){"XOR", SYSWORD(xor_w,0)};

    /* Arithmetics */
    *dictptr++ = (DictEntry){"=", SYSWORD(equ_w,0)};
    *dictptr++ = (DictEntry){"%", SYSWORD(mod_w,0)};
    *dictptr++ = (DictEntry){"+", SYSWORD(sum_w,0)};
    *dictptr++ = (DictEntry){"-", SYSWORD(sub_w,0)};
    *dictptr++ = (DictEntry){"/", SYSWORD(div_w,0)};
    *dictptr++ = (DictEntry){"*", SYSWORD(mul_w,0)};
    *dictptr++ = (DictEntry){"+F", SYSWORD(fsum_w,0)};
    *dictptr++ = (DictEntry){"-F", SYSWORD(fsub_w,0)};
    *dictptr++ = (DictEntry){"/F", SYSWORD(fdiv_w,0)};
    *dictptr++ = (DictEntry){"*F", SYSWORD(fmul_w,0)};

    userwords = dictptr;
}


void repl() {
   char *line = NULL;
   size_t line_sz = 0;

   // Loop
   while (1) {
       // Read
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
       ok(line_sz);
    }
}

int main() {
    init_dict();
    repl();
    bye_w();
}

