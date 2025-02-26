#!/bin/env python3

# This is an implementation of FORTH programming language.
# It is supposed to be simple, flexible, elegant.
# NOTE from 22.02.2025: this thing did turn out to be very flexible! Adding new
# words is so simple!
# It will not copy common implementations just for sake of keeping things the same.


STACK = []
WORD_DICT = {}
COMPILE_MODE = False
COMPILE_TARGET = ''
COMPILE_STACK = []
jumpwords = 0
pointer = 0
expression = ''

def ins_arguments(req, got):
    print("PROVIDE", req, "NOT", got)

def askstack(num):
    if len(STACK) < num:
        ins_arguments(num, len(STACK))
        return False
    return True

def asknum(*args):
    for a in args:
        if type(a) != int and type(a) != float:
            print("EXPECTED NUMBER GOT", type(a))
            return False
    return True

def askstr(*args):
    for a in args:
        if type(a) != str:
            print("EXPECTED STRING GOT", type(a))
            return False
    return True

def swap_wf():
    STACK[-1],STACK[-2] = STACK[-2],STACK[-1]

def rot_wf():
    STACK[-3],STACK[-2],STACK[-1] = STACK[-1],STACK[-3],STACK[-2]

def words_wf():
    for w in WORD_DICT:
        print(w, end=' ')
    print()

def sum_wf():
    if not askstack(2): return
    a, b, = STACK.pop(), STACK.pop()
    if not asknum(a, b): return
    STACK.append(a+b)

def sub_wf():
    if not askstack(2): return
    b, a, = STACK.pop(), STACK.pop()
    if not asknum(a, b): return
    STACK.append(a-b)

def mul_wf():
    if not askstack(2): return
    b, a, = STACK.pop(), STACK.pop()
    if not asknum(a, b): return
    STACK.append(a*b)
    
def div_wf():
    if not askstack(2): return
    b, a, = STACK.pop(), STACK.pop()
    if not asknum(a, b): return
    STACK.append(a/b)
    
def putnum_wf():
    if not askstack(1): return
    a = STACK.pop()
    if not asknum(a): return
    print(a, end=' ')

def emit_wf():
    if not askstack(1): return
    a = STACK.pop()
    if not asknum(a): return
    print(chr(a), end='')

def cr_wf():
    print()

def word_wf():
    global pointer
    global expression
    word = ''

    if pointer >= len(expression):
        print("NO WORD TO SCAN")
        return
    
    while not (c:=expression[pointer]).isspace():
        pointer += 1
        word += c

    if word == '': print("WARNING:EMPTY WORD")

    # IMPORTANT: in C/C++ implementation this will be different. In python it is easier to just push strings onto stack, not pointers to them.
    STACK.append(word)

def colon_wf():
    global COMPILE_MODE
    global COMPILE_TARGET
    global COMPILE_STACK
    
    # Grab word name
    word_wf()
    if not askstack(1): return

    COMPILE_MODE = True
    COMPILE_TARGET = STACK.pop().upper()
    COMPILE_STACK = []

    WORD_DICT[COMPILE_TARGET] = {
        "name": COMPILE_TARGET,
        "normal": NORMAL,
        "typ": COLON,
        "definition": []
    }

def scolon_wf():
    global COMPILE_MODE
    global COMPILE_TARGET
    global COMPILE_STACK

    if not COMPILE_MODE:
        print("NOT IN COMPILE MODE")
        return
    
    COMPILE_MODE = False
    WORD_DICT[COMPILE_TARGET]['definition'] = COMPILE_STACK

def immediate_wf():
    if not COMPILE_MODE:
        print("NOT IN COMPILE MODE")
        return
    WORD_DICT[COMPILE_TARGET]['normal'] = IMMEDIATE

# Unconditionally jump N words forward or backward
def branch_wf():
    global jumpwords

    if not askstack(1): return
    a = STACK.pop()
    if not asknum(a): return

    jumpwords = a

# Conditionally jump N forward or backward
def condbranch_wf():
    global jumpwords

    if not askstack(2): return
    a, cond = STACK.pop(), STACK.pop()
    if not asknum(a, cond): return

    if cond == 0: return

    jumpwords = a

def bye_wf():
    print("BYE")
    exit(0)
    
# END PREDEF FUNCTIONS

def new_word():
    return {name:"",normal:NORMAL, typ:COLON, definition:None}

def read_input():
    inp = input() + ' ' #Disgusting little hack
    return inp

def evaluate(expr):
    global expression
    expression = expr
    global pointer
    pointer = 0

    word = ''
    
    while pointer < len(expr):
        c = expr[pointer]
        pointer += 1
        
        if c.isspace():
            word = word.upper()
            # Beginning
            if word == '': continue

            if word in WORD_DICT or (is_number(word) and COMPILE_MODE):
                handle_word(word)
            elif t := is_number(word):
                STACK.append(getnum(word, t))
            else:
                print(word, end=' ')
                return -1

            word = ''
            continue

        word += c

#def nonone(x):
#    return 0 if x == None else x

def handle_word(w):
    execute_word(WORD_DICT[w]) if not COMPILE_MODE else compile_word(w)

def compile_word(w):
    global COMPILE_STACK

    if immediate(w):
        execute_word(WORD_DICT[w])
        return

    if t := is_number(w):
        COMPILE_STACK.append(getnum(w, t))
        return
    
    COMPILE_STACK.append(w)

def immediate(w):
    if not w in WORD_DICT: return False
    #print(w, WORD_DICT[w])
    return WORD_DICT[w]["normal"] == IMMEDIATE

def execute_word(word_def):
    global jumpwords

    if word_def["typ"] == PREDEF:
        word_def["definition"]()
        return

    ptr = 0
    while ptr < len(word_def['definition']):
        w = word_def['definition'][ptr]
        ptr += 1
        if type(w) != str:
            STACK.append(w)
            continue
        execute_word(WORD_DICT[w])
        if jumpwords != 0:
            ptr += jumpwords
            jumpwords = 0
        
def getnum(word, t):
    return int(word) if t==1 else float(word)

def is_number(word):
    dot_count = 0
    sign_count = 0
    cc = 0
    for c in word:
        cc += 1
        if c == '.': dot_count += 1
        if c == '-': sign_count += 1
        elif not c.isdigit(): return 0

    if cc-dot_count < 1: return 0
    if cc-sign_count < 1: return 0

    # Built-in int/float distinguisher!
    return dot_count+1 if dot_count < 2 else 0

def prompt(err):
    # TODO: Retract newline
    print("?" if err else "ok", end=' ')
    if len(STACK) > 0: print(len(STACK), end=' ')

def repl():
    try:
        while True:
            err = evaluate(read_input())
            prompt(err)
    except KeyboardInterrupt:
        print("INTERRUPT")
        repl()
    except EOFError:
        bye_wf()

#### PREDEFINITIONS BEGIN ####
        
NORMAL, IMMEDIATE = 0, 1
PREDEF, COLON = 0, 1
# WORD DEFINTION STRUCTURE!
# {"NAME", "NORMAL"/IMMEDIATE, PREDEF/COLON, "DEFINITION"}
DROP_W = {"name":"DROP", "normal":NORMAL, "typ":PREDEF, "definition": (lambda: STACK.pop())}
DUP_W  = {"name":"DUP", "normal":NORMAL, "typ":PREDEF, "definition": (lambda: STACK.append(STACK[-1]))}
SWAP_W = {"name":"SWAP", "normal":NORMAL, "typ":PREDEF, "definition": swap_wf}
ROT_W = {"name":"ROT", "normal":NORMAL, "typ":PREDEF, "definition":rot_wf}
PUTNUM_W = {"name":".", "normal":NORMAL, "typ":PREDEF, "definition": putnum_wf}
EMIT_W = {"name":"EMIT", "normal":NORMAL, "typ":PREDEF, "definition": emit_wf}
CR_W = {"name":"CR", "normal":NORMAL, "typ":PREDEF, "definition": cr_wf}
WORD_W = {"name":"WORD", "normal":NORMAL, "typ":PREDEF, "definition": word_wf}
SUM_W = {"name":"+", "normal":NORMAL, "typ":PREDEF, "definition": sum_wf}
SUB_W = {"name":"-", "normal":NORMAL, "typ":PREDEF, "definition": sub_wf}
MUL_W = {"name":"*", "normal":NORMAL, "typ":PREDEF, "definition": mul_wf}
DIV_W = {"name":"/", "normal":NORMAL, "typ":PREDEF, "definition": div_wf}
WORDS_W = {"name":"WORDS", "normal":NORMAL, "typ":PREDEF, "definition": words_wf}
COLON_W = {"name":":", "normal":NORMAL, "typ":PREDEF, "definition": colon_wf}
SCOLON_W = {"name":";", "normal":IMMEDIATE, "typ":PREDEF, "definition": scolon_wf}
IMMEDIATE_W = {"name":"IMMEDIATE", "normal":IMMEDIATE, "typ":PREDEF, "definition": immediate_wf}
BRANCH_W = {"name":"BRANCH", "normal":NORMAL, "typ":PREDEF, "definition": branch_wf}
CONDBRANCH_W = {"name":"?BRANCH", "normal":NORMAL, "typ":PREDEF, "definition":
                condbranch_wf}
BYE_W = {"name":"BYE", "normal":NORMAL, "typ":PREDEF, "definition": bye_wf}

WORD_DICT = {"DROP": DROP_W,
             "DUP":  DUP_W,
             "SWAP": SWAP_W,
             "ROT":  ROT_W,
             ".":    PUTNUM_W,
             "EMIT": EMIT_W,
             "CR":   CR_W,
             "WORD": WORD_W,
             "+":    SUM_W,
             "-":    SUB_W,
             "*":    MUL_W,
             "/":    DIV_W,
             "WORDS":WORDS_W,
             ":":    COLON_W,
             ";":    SCOLON_W,
             "IMMEDIATE": IMMEDIATE_W,
             "BRANCH": BRANCH_W,
             "?BRANCH": CONDBRANCH_W,
             "BYE":  BYE_W}

#### PREDEFINITIONS END ####

# if
# cond if <then> else <otherwise> endif
# cond <elseflag> findflag !?branch <then> <endifflag> findflag branch <elseflag> <otherwise> <endifflag>
# <elseflag> = %1
# <endifflag> = %2
# : if immediate    %1 findflag swap not swap ?branch ;
# : else immediate  %2 findflag branch %else ; TODO: figure out how to define
# this stuff
# : endif immediate %2 ;

repl()
