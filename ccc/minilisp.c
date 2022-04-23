// This software is in the public domain.

#include <assert.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

static __attribute((noreturn)) void error(char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    exit(1);
}

//======================================================================
// Lisp objects
//======================================================================

// The Lisp object type
enum {
    // Regular objects visible from the user
    TINT = 1,  // 整數
    TCELL,     // TCELL 就是一般的 LIST
    TSYMBOL,   // 符號
    TPRIMITIVE,// 基本函數
    TFUNCTION, // 自訂函數
    TMACRO,    // 巨集
    TENV,      // 環境變數 frame
    // The marker that indicates the object has been moved to other location by GC. The new location
    // can be found at the forwarding pointer. Only the functions to do garbage collection set and
    // handle the object of this type. Other functions will never see the object of this type.
    TMOVED,    // 垃圾蒐集時標示已經被移動過了
    // Const objects. They are statically allocated and will never be managed by GC.
    TTRUE,     // TRUE
    TNIL,      // Nil (FALSE)
    TDOT,      // .
    TCPAREN,   // )
};

// Typedef for the primitive function
struct Obj;
typedef struct Obj *Primitive(void *root, struct Obj **env, struct Obj **args);

// The object type
typedef struct Obj {
    // The first word of the object represents the type of the object. Any code that handles object
    // needs to check its type first, then access the following union members.
    int type; // 物件型態

    // The total size of the object, including "type" field, this field, the contents, and the
    // padding at the end of the object.
    int size; // 物件大小

    // Object values.
    union {
        // Int
        int value; // 整數物件值
        // Cell    // 一般 LIST
        struct {
            struct Obj *car;
            struct Obj *cdr;
        };
        // Symbol     // 符號名稱
        char name[1]; // 奇怪，為何不是指標，長度只宣告 1 ??
        // 原因是 alloc() 函數會分配大小，而這是最後一個欄位，所以自然會分配足夠，不用擔心！
        // Primitive
        Primitive *fn; // 基本函數
        // Function or Macro
        struct {
            struct Obj *params; // 巨集參數
            struct Obj *body;   // 巨集 body
            struct Obj *env;    // 巨集 env 環境變數
        };
        // Environment frame. This is a linked list of association lists
        // containing the mapping from symbols to their value.
        struct { // 環境變數
            struct Obj *vars;
            struct Obj *up;
        };
        // Forwarding pointer
        void *moved; // 移動後物件的新位址
    };
} Obj;

// Constants
static Obj *True = &(Obj){ TTRUE };
static Obj *Nil = &(Obj){ TNIL };
static Obj *Dot = &(Obj){ TDOT };
static Obj *Cparen = &(Obj){ TCPAREN };

// The list containing all symbols. Such data structure is traditionally called the "obarray", but I
// avoid using it as a variable name as this is not an array but a list.
static Obj *Symbols;

#include "gc.c"
//======================================================================
// Constructors
//======================================================================

static Obj *make_int(void *root, int value) { // 創建整數
    Obj *r = alloc(root, TINT, sizeof(int));
    r->value = value;
    return r;
}

static Obj *cons(void *root, Obj **car, Obj **cdr) { // 創建 cell = cons(car,cdr)
    Obj *cell = alloc(root, TCELL, sizeof(Obj *) * 2);
    cell->car = *car;
    cell->cdr = *cdr;
    return cell;
}

static Obj *make_symbol(void *root, char *name) { // 創建符號變數
    Obj *sym = alloc(root, TSYMBOL, strlen(name) + 1);
    strcpy(sym->name, name);
    return sym;
}

static Obj *make_primitive(void *root, Primitive *fn) { // 創建基本函數
    Obj *r = alloc(root, TPRIMITIVE, sizeof(Primitive *));
    r->fn = fn;
    return r;
}

static Obj *make_function(void *root, Obj **env, int type, Obj **params, Obj **body) { // 創建函數
    assert(type == TFUNCTION || type == TMACRO);
    Obj *r = alloc(root, type, sizeof(Obj *) * 3);
    r->params = *params;
    r->body = *body;
    r->env = *env;
    return r;
}

struct Obj *make_env(void *root, Obj **vars, Obj **up) { // push_env 裏呼叫 make_env(root, map, env)
    Obj *r = alloc(root, TENV, sizeof(Obj *) * 2);
    r->vars = *vars; // vars=map: 新函數的 frame
    r->up = *up; // up=env: 連接到上層的 frame
    return r;
}

// Returns ((x . y) . a)
static Obj *acons(void *root, Obj **x, Obj **y, Obj **a) {
    DEFINE1(cell);
    *cell = cons(root, x, y);
    return cons(root, cell, a);
}

#include "parser.c"

// Prints the given object.
static void print(Obj *obj) { // 印出一個物件 (list)
    switch (obj->type) {
    case TCELL: // 遞迴印出 list
        printf("(");
        for (;;) {
            print(obj->car);
            if (obj->cdr == Nil)
                break;
            if (obj->cdr->type != TCELL) {
                printf(" . ");
                print(obj->cdr);
                break;
            }
            printf(" ");
            obj = obj->cdr;
        }
        printf(")");
        return;

#define CASE(type, ...)                         \
    case type:                                  \
        printf(__VA_ARGS__);                    \
        return
    CASE(TINT, "%d", obj->value);
    CASE(TSYMBOL, "%s", obj->name);
    CASE(TPRIMITIVE, "<primitive>");
    CASE(TFUNCTION, "<function>");
    CASE(TMACRO, "<macro>");
    CASE(TMOVED, "<moved>");
    CASE(TTRUE, "t");
    CASE(TNIL, "()");
#undef CASE
    default:
        error("Bug: print: Unknown tag type: %d", obj->type);
    }
}

// Returns the length of the given list. -1 if it's not a proper list.
static int length(Obj *list) { // 傳回 list 長度
    int len = 0;
    for (; list->type == TCELL; list = list->cdr)
        len++;
    return list == Nil ? len : -1;
}

//======================================================================
// Evaluator
//======================================================================

static Obj *eval(void *root, Obj **env, Obj **obj);

static void add_variable(void *root, Obj **env, Obj **sym, Obj **val) { // 新增變數
    DEFINE2(vars, tmp);
    *vars = (*env)->vars;
    *tmp = acons(root, sym, val, vars);
    (*env)->vars = *tmp;
}

// Returns a newly created environment frame. // 這就是造成 closure 的 frame
static Obj *push_env(void *root, Obj **env, Obj **vars, Obj **vals) {
    DEFINE3(map, sym, val);
    *map = Nil;
    for (; (*vars)->type == TCELL; *vars = (*vars)->cdr, *vals = (*vals)->cdr) {
        if ((*vals)->type != TCELL)
            error("Cannot apply function: number of argument does not match");
        *sym = (*vars)->car;
        *val = (*vals)->car;
        *map = acons(root, sym, val, map);
    }
    if (*vars != Nil)
        *map = acons(root, vars, vals, map);
    return make_env(root, map, env); // 注意，map 是新函數的 frame，連接到舊的 env ??
}
// (progn expr ...) 其實就是 ((lambda () expr ...))，也就是無參數的函數。
// Evaluates the list elements from head and returns the last return value.
static Obj *progn(void *root, Obj **env, Obj **list) { // 執行整個程式
    DEFINE2(lp, r);
    for (*lp = *list; *lp != Nil; *lp = (*lp)->cdr) {
        *r = (*lp)->car;
        *r = eval(root, env, r);
    }
    return *r;
}

// Evaluates all the list elements and returns their return values as a new list.
static Obj *eval_list(void *root, Obj **env, Obj **list) { // 執行一個 list
    DEFINE4(head, lp, expr, result);
    *head = Nil;
    for (lp = list; *lp != Nil; *lp = (*lp)->cdr) {
        *expr = (*lp)->car;
        *result = eval(root, env, expr);
        *head = cons(root, result, head);
    }
    return reverse(*head);
}

static bool is_list(Obj *obj) {
    return obj == Nil || obj->type == TCELL; // TCELL 就是 list 的型態
}

static Obj *apply_func(void *root, Obj **env, Obj **fn, Obj **args) { // 呼叫 fn(args)
    DEFINE3(params, newenv, body);
    *params = (*fn)->params;
    *newenv = (*fn)->env;
    *newenv = push_env(root, newenv, params, args); // 每個函數都會創建一個新的 frame (env),把 params 綁到 args
    // 注意： env 是附屬於 fn 的，每個函數都有自己的 env
    *body = (*fn)->body;
    return progn(root, newenv, body); // 執行 body
}

// Apply fn with args.
static Obj *apply(void *root, Obj **env, Obj **fn, Obj **args) { // 呼叫 fn(args)
    if (!is_list(*args))
        error("argument must be a list");
    if ((*fn)->type == TPRIMITIVE)
        return (*fn)->fn(root, env, args);
    if ((*fn)->type == TFUNCTION) {
        DEFINE1(eargs);
        *eargs = eval_list(root, env, args);
        return apply_func(root, env, fn, eargs);
    }
    error("not supported");
}

// Searches for a variable by symbol. Returns null if not found.
static Obj *find(Obj **env, Obj *sym) { // 查表取出符號值 (因為上層都被包進來了，所以不用檢查整個 env stack ?)
    for (Obj *p = *env; p != Nil; p = p->up) {
        for (Obj *cell = p->vars; cell != Nil; cell = cell->cdr) {
            Obj *bind = cell->car;
            if (sym == bind->car)
                return bind;
        }
    }
    return NULL;
}

// Expands the given macro application form. // 範例: (macroexpand (if-zero x (print x)))
static Obj *macroexpand(void *root, Obj **env, Obj **obj) {
    if ((*obj)->type != TCELL || (*obj)->car->type != TSYMBOL)
        return *obj;
    DEFINE3(bind, macro, args);
    *bind = find(env, (*obj)->car); // 巨集名稱，例如 if-zero
    if (!*bind || (*bind)->cdr->type != TMACRO)
        return *obj;
    *macro = (*bind)->cdr; // 巨集 BODY，例如 (print x)
    *args = (*obj)->cdr;   // 巨集參數，例如 x
    return apply_func(root, env, macro, args); // 展開該巨集
}

// Evaluates the S expression.
static Obj *eval(void *root, Obj **env, Obj **obj) {
    switch ((*obj)->type) {
    case TINT:
    case TPRIMITIVE:
    case TFUNCTION:
    case TTRUE:
    case TNIL:
        // Self-evaluating objects
        return *obj; // 基本型態，直接傳回
    case TSYMBOL: { 
        // Variable
        Obj *bind = find(env, *obj); // 變數，查出其值後傳回
        if (!bind)
            error("Undefined symbol: %s", (*obj)->name);
        return bind->cdr;
    }
    case TCELL: {
        // Function application form // 函數
        DEFINE3(fn, expanded, args);
        *expanded = macroexpand(root, env, obj); // 先執行巨集展開
        if (*expanded != *obj) // 若展開後不同
            return eval(root, env, expanded); // 則運算展開後的式子
        *fn = (*obj)->car; // 否則取出頭部
        *fn = eval(root, env, fn); // 運算頭部
        *args = (*obj)->cdr; // 再取出尾部
        if ((*fn)->type != TPRIMITIVE && (*fn)->type != TFUNCTION) // 頭部必須是函數
            error("The head of a list must be a function");
        return apply(root, env, fn, args); // 執行該函數 fn(args)
    }
    default:
        error("Bug: eval: Unknown tag type: %d", (*obj)->type);
    }
}

//======================================================================
// Primitive functions and special forms
//======================================================================

// 'expr
static Obj *prim_quote(void *root, Obj **env, Obj **list) {
    if (length(*list) != 1)
        error("Malformed quote");
    return (*list)->car; // 傳回 expr，不 eval()
}

// (cons expr expr)
static Obj *prim_cons(void *root, Obj **env, Obj **list) {
    if (length(*list) != 2)
        error("Malformed cons");
    Obj *cell = eval_list(root, env, list);
    cell->cdr = cell->cdr->car; // 原本 (expr1 (expr1 ...)) 變成 (expr1 expr1)
    return cell;
}

// (car <cell>)
static Obj *prim_car(void *root, Obj **env, Obj **list) {
    Obj *args = eval_list(root, env, list);
    if (args->car->type != TCELL || args->cdr != Nil)
        error("Malformed car");
    return args->car->car; // 取得 cell 的頭部
}

// (cdr <cell>)
static Obj *prim_cdr(void *root, Obj **env, Obj **list) {
    Obj *args = eval_list(root, env, list);
    if (args->car->type != TCELL || args->cdr != Nil)
        error("Malformed cdr");
    return args->car->cdr; // 取得 cell 的尾部
}

// (setq <symbol> expr)
static Obj *prim_setq(void *root, Obj **env, Obj **list) {
    if (length(*list) != 2 || (*list)->car->type != TSYMBOL)
        error("Malformed setq");
    DEFINE2(bind, value);
    *bind = find(env, (*list)->car);
    if (!*bind)
        error("Unbound variable %s", (*list)->car->name);
    *value = (*list)->cdr->car;
    *value = eval(root, env, value);
    (*bind)->cdr = *value; // 設定 symbol 為 expr
    return *value;
}

// (setcar <cell> expr)
static Obj *prim_setcar(void *root, Obj **env, Obj **list) {
    DEFINE1(args);
    *args = eval_list(root, env, list);
    if (length(*args) != 2 || (*args)->car->type != TCELL)
        error("Malformed setcar");
    (*args)->car->car = (*args)->cdr->car; // 設定 cell 為 expr
    return (*args)->car;
}

// (while cond expr ...)
static Obj *prim_while(void *root, Obj **env, Obj **list) {
    if (length(*list) < 2)
        error("Malformed while");
    DEFINE2(cond, exprs);
    *cond = (*list)->car;
    while (eval(root, env, cond) != Nil) { // 當 cond 成立
        *exprs = (*list)->cdr;
        eval_list(root, env, exprs);       // 執行所有 expr
    }
    return Nil;
}

// (gensym)
static Obj *prim_gensym(void *root, Obj **env, Obj **list) {
  static int count = 0;
  char buf[10];
  snprintf(buf, sizeof(buf), "G__%d", count++);
  return make_symbol(root, buf); // 創建唯一符號
}

// (+ <integer> ...)
static Obj *prim_plus(void *root, Obj **env, Obj **list) {
    int sum = 0;
    for (Obj *args = eval_list(root, env, list); args != Nil; args = args->cdr) {
        if (args->car->type != TINT)
            error("+ takes only numbers");
        sum += args->car->value;
    } // int1+int2+...+intN
    return make_int(root, sum);
}

// (- <integer> ...)
static Obj *prim_minus(void *root, Obj **env, Obj **list) {
    Obj *args = eval_list(root, env, list);
    for (Obj *p = args; p != Nil; p = p->cdr)
        if (p->car->type != TINT)
            error("- takes only numbers");
    if (args->cdr == Nil)
        return make_int(root, -args->car->value);
    int r = args->car->value;
    for (Obj *p = args->cdr; p != Nil; p = p->cdr) // int1-int2...-intN
        r -= p->car->value;
    return make_int(root, r);
}

// (< <integer> <integer>)
static Obj *prim_lt(void *root, Obj **env, Obj **list) {
    Obj *args = eval_list(root, env, list);
    if (length(args) != 2)
        error("malformed <");
    Obj *x = args->car;
    Obj *y = args->cdr->car;
    if (x->type != TINT || y->type != TINT)
        error("< takes only numbers");
    return x->value < y->value ? True : Nil; // 是否 integer1 < integer2 ?
}

static Obj *handle_function(void *root, Obj **env, Obj **list, int type) {
    if ((*list)->type != TCELL || !is_list((*list)->car) || (*list)->cdr->type != TCELL)
        error("Malformed lambda");
    Obj *p = (*list)->car;
    for (; p->type == TCELL; p = p->cdr)
        if (p->car->type != TSYMBOL)
            error("Parameter must be a symbol");
    if (p != Nil && p->type != TSYMBOL)
        error("Parameter must be a symbol");
    DEFINE2(params, body);  // 分配兩個空間
    *params = (*list)->car; // 1. 參數 params
    *body = (*list)->cdr;   // 2. 程式 body
    return make_function(root, env, type, params, body); // 建立該函數
}

// (lambda (<symbol> ...) expr ...)
static Obj *prim_lambda(void *root, Obj **env, Obj **list) {
    return handle_function(root, env, list, TFUNCTION); // 定義函數，不記錄名稱
}

static Obj *handle_defun(void *root, Obj **env, Obj **list, int type) {
    if ((*list)->car->type != TSYMBOL || (*list)->cdr->type != TCELL)
        error("Malformed defun");
    DEFINE3(fn, sym, rest);
    *sym = (*list)->car;
    *rest = (*list)->cdr;
    *fn = handle_function(root, env, rest, type); // 創建函數
    add_variable(root, env, sym, fn); // 其名稱為 sym
    return *fn;
}

// (defun <symbol> (<symbol> ...) expr ...) // 例如：(defun list (x . y) (cons x y))
static Obj *prim_defun(void *root, Obj **env, Obj **list) {
    return handle_defun(root, env, list, TFUNCTION); // 定義函數
}

// (define <symbol> expr) // 例如：(define board (make-board board-size))
static Obj *prim_define(void *root, Obj **env, Obj **list) {
    if (length(*list) != 2 || (*list)->car->type != TSYMBOL)
        error("Malformed define");
    DEFINE2(sym, value);
    *sym = (*list)->car;
    *value = (*list)->cdr->car;
    *value = eval(root, env, value);
    add_variable(root, env, sym, value); // 定義 symbol 為 expr 的值
    return *value;
}

// (defmacro <symbol> (<symbol> ...) expr ...)
static Obj *prim_defmacro(void *root, Obj **env, Obj **list) {
    return handle_defun(root, env, list, TMACRO); // 定義巨集函數
}

// (macroexpand expr)
static Obj *prim_macroexpand(void *root, Obj **env, Obj **list) {
    if (length(*list) != 1)
        error("Malformed macroexpand");
    DEFINE1(body);
    *body = (*list)->car;
    return macroexpand(root, env, body); // 巨集展開
}

// (println expr)
static Obj *prim_println(void *root, Obj **env, Obj **list) {
    DEFINE1(tmp);
    *tmp = (*list)->car;
    print(eval(root, env, tmp)); // 印出 expr1
    printf("\n");
    return Nil;
}

// (if expr expr expr ...)
static Obj *prim_if(void *root, Obj **env, Obj **list) {
    if (length(*list) < 2)
        error("Malformed if");
    DEFINE3(cond, then, els);
    *cond = (*list)->car;
    *cond = eval(root, env, cond);   // 檢查 expr1
    if (*cond != Nil) {              // 若不是 Nil // =()=False
        *then = (*list)->cdr->car;
        return eval(root, env, then); // 則執行 expr2 (then)
    }
    *els = (*list)->cdr->cdr;
    return *els == Nil ? Nil : progn(root, env, els); // 否則執行 expr3 (else)
}

// (= <integer> <integer>)
static Obj *prim_num_eq(void *root, Obj **env, Obj **list) {
    if (length(*list) != 2)
        error("Malformed =");
    Obj *values = eval_list(root, env, list);
    Obj *x = values->car;
    Obj *y = values->cdr->car;
    if (x->type != TINT || y->type != TINT)
        error("= only takes numbers");
    return x->value == y->value ? True : Nil; // 檢查 integer1 == integer2 ?
}

// (eq expr expr)
static Obj *prim_eq(void *root, Obj **env, Obj **list) {
    if (length(*list) != 2)
        error("Malformed eq");
    Obj *values = eval_list(root, env, list);
    return values->car == values->cdr->car ? True : Nil; // 檢查 expr1 == expr2 ?
}

static void add_primitive(void *root, Obj **env, char *name, Primitive *fn) {
    DEFINE2(sym, prim);
    *sym = intern(root, name);
    *prim = make_primitive(root, fn); // 創建 name 符號
    add_variable(root, env, sym, prim); // 新增基本函數
}

static void define_constants(void *root, Obj **env) {
    DEFINE1(sym);
    *sym = intern(root, "t"); // 創建 t 符號
    add_variable(root, env, sym, &True); // 新增常數 t (True)
}

static void define_primitives(void *root, Obj **env) { // 創建所有預設的 symbol 函數
    add_primitive(root, env, "quote", prim_quote);
    add_primitive(root, env, "cons", prim_cons);
    add_primitive(root, env, "car", prim_car);
    add_primitive(root, env, "cdr", prim_cdr);
    add_primitive(root, env, "setq", prim_setq);
    add_primitive(root, env, "setcar", prim_setcar);
    add_primitive(root, env, "while", prim_while);
    add_primitive(root, env, "gensym", prim_gensym);
    add_primitive(root, env, "+", prim_plus);
    add_primitive(root, env, "-", prim_minus);
    add_primitive(root, env, "<", prim_lt);
    add_primitive(root, env, "define", prim_define);
    add_primitive(root, env, "defun", prim_defun);
    add_primitive(root, env, "defmacro", prim_defmacro);
    add_primitive(root, env, "macroexpand", prim_macroexpand);
    add_primitive(root, env, "lambda", prim_lambda);
    add_primitive(root, env, "if", prim_if);
    add_primitive(root, env, "=", prim_num_eq);
    add_primitive(root, env, "eq", prim_eq);
    add_primitive(root, env, "println", prim_println);
}

//======================================================================
// Entry point
//======================================================================

// Returns true if the environment variable is defined and not the empty string.
static bool getEnvFlag(char *name) { // 判斷環境變數是否存在？
    char *val = getenv(name);
    return val && val[0];
}

int main(int argc, char **argv) {
    // Debug flags
    debug_gc = getEnvFlag("MINILISP_DEBUG_GC");
    always_gc = getEnvFlag("MINILISP_ALWAYS_GC");

    // Memory allocation
    memory = alloc_semispace();

    // Constants and primitives
    Symbols = Nil;
    void *root = NULL;
    DEFINE2(env, expr);
    *env = make_env(root, &Nil, &Nil); // 創建 root 環境
    define_constants(root, env);  // 定義常數
    define_primitives(root, env); // 定義基本運算

    // The main loop // 主迴圈
    for (;;) {
        *expr = read_expr(root); // 讀取一個運算式 (...)
        if (!*expr) // 沒有運算式了，離開！
            return 0;
        if (*expr == Cparen)
            error("Stray close parenthesis"); // 括號位置錯誤 )
        if (*expr == Dot) // 點 . 位置錯誤
            error("Stray dot");
        print(eval(root, env, expr)); // 執行該運算式
        printf("\n");
    }
}
