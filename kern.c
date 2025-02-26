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
    FL_PREDEFINED = (1<<2),
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

#define DICT_SIZE 128 
typedef struct { char *name; Word w; } DictEntry;
DictEntry dict[DICT_SIZE] = {0}, *dictptr = dict;


char *expression = NULL;
char *pointer = NULL;
// 0 = interpreting
// 1 = compiling
char state = 0;

//TODO: execution trace for branching back
//Use Thing*

// Report insufficient arguments
void ins_arguments(size_t req, size_t got) {
    printf("EXPECTED %lu GOT %lu\n", req, got);
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
        printf("Error: stack overflow\n");
        return 1;
    }

    return 0;
}

/* Begin predefined words *******/
void bye_w() {
    printf("--BYE-----------------------------------\n");
    exit(0);
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

char *parse();
void colon_w() {
    char *name = parse();

    // Capitalize
    for (char *np = name; *np; ++np) {
        *np = toupper(*np);
    }

    // Copy name
    dictptr->name = malloc(strlen(name) + 1);
    strcpy(dictptr->name, name);

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
    dictptr->w = (Word){0,sz,.body=definition};

    dictptr++;

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
    const size_t stack_sz = (stackptr-stack);

    // Print stack size
    if (stack_sz) {
        printf("\033[A\033[%dGok %lu\n", line_sz+1, stack_sz);
        return;
    }

    // Just ok
    printf("\033[A\033[%dGok\n", line_sz+1);
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
    // Check overflow
    if ((compstackptr-compstack) >= COMPSTACK_SIZE) {
        fprintf(stderr, "Compilation stack overflow -- word is too long!\n");
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

// Execute a thing from a userword
void execute(Word *w);
void execute_thing(Thing th) {
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

    // Should not be executed
    if (w->flags & FL_COMPONLY) {
        fprintf(stderr, "WORD IS COMPILE-ONLY AT %lu\n", pointer-expression);
        return;
    }
    
    // Predefined words
    if (w->flags & FL_PREDEFINED) {
        w->sw();
        return;
    }

    // User words
    for (size_t i = 0; i < w->size; i++) {
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
    
    fprintf(stderr, "'%s'?\n", word);
    return NULL;
}

void eval() {
   while(1) {
       char *word = parse();
       // EOL
       if (word == NULL) break;
       if (*word == 0) break;

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

       if (state && !(w->flags & FL_IMMEDIATE)) compile(w);
       else execute(w);
   }
}


// Dictionary initialization
void init_dict() {
    /* Basics */
    *dictptr++ = (DictEntry){"BYE", SYSWORD(bye_w,0)};
    *dictptr++ = (DictEntry){":", SYSWORD(colon_w,FL_IMMEDIATE)};
    *dictptr++ = (DictEntry){";", SYSWORD(scolon_w,FL_IMMEDIATE)};
    *dictptr++ = (DictEntry){"IMMEDIATE", SYSWORD(immediate_w,FL_IMMEDIATE|FL_COMPONLY)};
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
       getline(&line, &line_sz, stdin);
       if (feof(stdin)) {
           free(line);
           exit(0);
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
}

