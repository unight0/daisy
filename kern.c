#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>

typedef union {
    char c;
    int64_t i;
    uint64_t u;
    double f;
    void *p;
} Cell;
typedef void (*SysWord)(void);
typedef struct Word Word;
typedef struct Thing Thing;

struct Thing {
   char type; 
   union {
        Cell c;
        Word *w;
   };
};

struct Word {
    char flags;
    size_t size;
    union {
        Thing *body;
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

#define STACK_SIZE 128
#define COMPSTACK_SIZE 256
Cell stack[STACK_SIZE];
// Compilation stack
// This means that the word size <= COMPSTACK_SIZE
Thing compstack[COMPSTACK_SIZE] = {0};
Thing *compstackptr = compstack;

// Topmost element
Cell *stackptr = stack;
char *comptarget = NULL;

#define DICT_SIZE 128 
typedef struct { char *name; Word w; } DictEntry;
DictEntry dict[DICT_SIZE] = {0}, *dictptr = dict;


char *expression = NULL;
char *pointer = NULL;
// 0 = interpreting
// 1 = compiling
char state = 0;
// Relative jump for branch and 0branch
// NOTE: absolute jump might be implemented later if needed.
// NOTE(2): this is basically timetravel
int64_t reljump = 0;

int error_happened = 0;
// Error reporitng
#define error(cs, ...){\
    error_happened = 1;\
    fprintf(stderr, "\033[m\033[1mAt %lu: ", pointer-expression);\
    fprintf(stderr, cs, ##__VA_ARGS__); \
    fprintf(stderr, "\033[m");}\

// Report insufficient arguments
void ins_arguments(size_t req, size_t got) {
    error( "Expected %lu got %lu\n", req, got);
}

// Ask to have NUM arguments on stack
int askstack(size_t num) {
    if ((stackptr-stack) < num) {
        ins_arguments(num, (stackptr-stack));
        return 1;
    }
    return 0;
}

// Ask to have NUM space left on stack arguments on stack
int askspace(size_t num) {
    const size_t current_size = (stackptr-stack);
    const size_t leftover = STACK_SIZE - current_size;

    if (leftover < num) {
        error( "Error: stack overflow\n");
        return 1;
    }

    return 0;
}

/* Begin predefined words *******/
void bye_w() {
    //printf("--BYE-----------------------------------\n");
    exit(0);
}

// N -- 
void branch_w() {
    if (askstack(1)) return;

    Cell n = *stackptr--;

    reljump = n.i;
}

// COND N --
// Branches when COND == 0
void zbranch_w() {
    if (askstack(1)) return;

    Cell n    = *stackptr--;
    Cell cond = *stackptr--;

    if(cond.i) return;

    reljump = n.i;
}

void putint_w() {
    if (askstack(1)) return;
    printf("%ld", stackptr--->i);
}
void putflt_w() {
    if(askstack(1)) return;
    printf("%f", stackptr--->f);
}
void emit_w() {
    if(askstack(1)) return;
    putchar(stackptr--->c);
}

void words_w() {
    for(DictEntry *p = dict; p < dictptr; p++) {
        printf("%s ", p->name);
    }
    printf("(total %lu/%lu)\n", dictptr-dict, DICT_SIZE);
}

// Adds entry if not present
void dict_entry(char *entry) {
    assert(entry != NULL);

    for (DictEntry *p = dict; p < dictptr; p++) {
        // Already present
        if (!strcmp(p->name, entry)) return;
    }

    // Add entry
    dictptr++->name = entry;
}

// Sets word at entry
void dict_setat(char *entry, Word w) {
    if (entry == NULL) {
        error("dict_setat: entry is null!\n");
        exit(1);
    }

    for (DictEntry *p = dict; p < dictptr; p++) {
        if (!strcmp(p->name, entry)) {
            p->w = w;
            return;
        }
    }

    error("dict_setat: dict entry '%s' doesn't exist!\n", entry);
    exit(1);
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
    const size_t sz = compstackptr-compstack;
    // Allocate
    Thing *definition = malloc(sz*sizeof(Thing));

    // Copy
    memcpy(definition, compstack, sz*sizeof(Thing));

    // Reset
    compstackptr = compstack;

    // Finish the word declaration
    Word w = (Word){0,sz,.body=definition};

    dict_setat(comptarget, w);
    comptarget = NULL;

    // Back into interpretation mode
    state = 0;
}

// Adds FL_IMMEDIATE to the currently compiled word
void immediate_w() {
    dictptr->w.flags = FL_IMMEDIATE;
}

// Adds FL_COMPONLY to the currently compiled word
void compile_only_w() {
    dictptr->w.flags = FL_COMPONLY;
}

// Adds FL_INTERONLY to the currently compiled word
void runtime_only_w() {
    dictptr->w.flags = FL_INTERONLY;
}

// A B -- B A
void swap_w(void) {
    if(askstack(2)) return;

    Cell b = *stackptr--;
    Cell a = *stackptr--;

    *(++stackptr) = b;
    *(++stackptr) = a;
}

// A --
void drop_w(void) {
    if(askstack(1)) return;
    --stackptr;
}

// A B -- A B A
void over_w(void) {
    if(askstack(2)) return;

    Cell b = *stackptr--;
    Cell a = *stackptr--;

    *(++stackptr) = a;
    *(++stackptr) = b;
    *(++stackptr) = a;
}

// A B C -- B C A
void rot_w(void) {
    if(askstack(3)) return;

    Cell c = *stackptr--;
    Cell b = *stackptr--;
    Cell a = *stackptr--;

    *(++stackptr) = b;
    *(++stackptr) = c;
    *(++stackptr) = a;
}

/* Boring repetative arithmetic definitions below */

// Macros make it less repetative
#define TWOOP(a,b,t,v) if(askstack(2))return;\
    Cell a = *stackptr--;                    \
    Cell b = *stackptr--;                    \
    (++stackptr)->t = (v);

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
    if(askstack(1)) return;

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
    for(;!isspace(*pointer);++pointer);

    // Put endline
    *pointer = 0;
    ++pointer;

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
    // Check overflow
    if ((compstackptr-compstack) >= COMPSTACK_SIZE) {
        error( "Compilation stack overflow -- word is too long!\n");
        exit(1);
    }
    // Append the word
    *compstackptr++ = (Thing){TH_WORD,.w=w};
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
    *compstackptr++ = (Thing){TH_CELL,.c=num};
}

int jump() {
    // Skip word
    if (reljump > 0) {
        reljump--;
        return 1;
    }

    return 0;
}

// Execute a thing from a userword
void execute(Word *w);
void execute_thing(Thing th) {

    if (jump()) return;

    if (th.type & TH_CELL) {
        // Ensure that there is no stack overflow
        if (askspace(1)) exit(1);
        // Push
        *++stackptr = th.c;
        return;
    }

    // System word
    if (th.w->flags & FL_PREDEFINED) {
        th.w->sw();
        return;
    }

    // Userword
    // Note: recursive
    // TODO: rewrite non-recursively (?)
    execute(th.w); 
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
    for (size_t i = 0; i < w->size || reljump < 0; i++) {
        // Backtracking
        if (reljump < 0) {
            // For convenience
            reljump--;

            if (-reljump > i) {
                error("Cannot backtrack %ld words: too far back\n", reljump+1);
                reljump = 0;
                break;
            } 

            i += reljump;
            reljump = 0;
        }

        execute_thing(w->body[i]);
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
    *dictptr++ = (DictEntry){"BRANCH", SYSWORD(branch_w,FL_COMPONLY)};
    *dictptr++ = (DictEntry){"0BRANCH", SYSWORD(zbranch_w,FL_COMPONLY)};
    *dictptr++ = (DictEntry){":", SYSWORD(colon_w,FL_IMMEDIATE|FL_INTERONLY)};
    *dictptr++ = (DictEntry){";", SYSWORD(scolon_w,FL_IMMEDIATE|FL_COMPONLY)};
    *dictptr++ = (DictEntry){"IMMEDIATE", SYSWORD(immediate_w,FL_IMMEDIATE|FL_COMPONLY)};
    *dictptr++ = (DictEntry){"RUNTIME-ONLY", SYSWORD(runtime_only_w,FL_IMMEDIATE|FL_COMPONLY)};
    *dictptr++ = (DictEntry){"COMPILE-ONLY", SYSWORD(compile_only_w,FL_IMMEDIATE|FL_COMPONLY)};

    /* Development */
    *dictptr++ = (DictEntry){"WORDS", SYSWORD(words_w,0)};

    /* Stack manipulation */
    // TODO: implement PICK
    *dictptr++ = (DictEntry){"SWAP", SYSWORD(swap_w,0)};
    *dictptr++ = (DictEntry){"DROP", SYSWORD(drop_w,0)};
    *dictptr++ = (DictEntry){"OVER", SYSWORD(over_w,0)};
    *dictptr++ = (DictEntry){"ROT", SYSWORD(rot_w,0)};

    /* I/O */
    //TODO: emit and . are not primitive enough
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
    *dictptr++ = (DictEntry){"+", SYSWORD(sum_w,0)};
    *dictptr++ = (DictEntry){"-", SYSWORD(sub_w,0)};
    *dictptr++ = (DictEntry){"/", SYSWORD(div_w,0)};
    *dictptr++ = (DictEntry){"*", SYSWORD(mul_w,0)};
    *dictptr++ = (DictEntry){"+F", SYSWORD(fsum_w,0)};
    *dictptr++ = (DictEntry){"-F", SYSWORD(fsub_w,0)};
    *dictptr++ = (DictEntry){"/F", SYSWORD(fdiv_w,0)};
    *dictptr++ = (DictEntry){"*F", SYSWORD(fmul_w,0)};
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
           exit(0);
       }
       if (ferror(stdin)) {
           perror("REPL");
           free(line);
           exit(1);
       }

       //line_sz = strlen(line);


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
}

