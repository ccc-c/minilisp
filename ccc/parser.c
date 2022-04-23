//======================================================================
// Parser (剖析器)
//
// This is a hand-written recursive-descendent parser. (遞迴下降法)
//======================================================================

#define SYMBOL_MAX_LEN 200
const char symbol_chars[] = "~!@#$%^&*-_=+:/?<>";

static Obj *read_expr(void *root);

static int peek(void) { // 偷看下一個字
    int c = getchar();
    ungetc(c, stdin);
    return c;
}

// Destructively reverses the given list.
static Obj *reverse(Obj *p) { // 反轉串列
    Obj *ret = Nil;
    while (p != Nil) {
        Obj *head = p;
        p = p->cdr;
        head->cdr = ret;
        ret = head;
    }
    return ret;
}

// Skips the input until newline is found. Newline is one of \r, \r\n or \n.
static void skip_line(void) { // 略過註解直到行尾
    for (;;) {
        int c = getchar();
        if (c == EOF || c == '\n')
            return;
        if (c == '\r') {
            if (peek() == '\n')
                getchar();
            return;
        }
    }
}

// Reads a list. Note that '(' has already been read.
static Obj *read_list(void *root) { // 讀取一個串列，字元 ( 已經讀了
    DEFINE3(obj, head, last);
    *head = Nil;
    for (;;) {
        *obj = read_expr(root); // 讀一個運算式
        if (!*obj)
            error("Unclosed parenthesis");
        if (*obj == Cparen) // 讀到 ) 了，反轉串列並傳回 (因為串列是倒著建立的)
            return reverse(*head);
        if (*obj == Dot) { // 遇到逗點 . 代表後面的東西全都要放入下一個變數
            *last = read_expr(root); // 讀下一個運算式
            if (read_expr(root) != Cparen) // . 後面一定只有一個 expr 就會接 )
                error("Closed parenthesis expected after dot");
            Obj *ret = reverse(*head); // 讀到 ) 了，反轉串列並傳回
            (*head)->cdr = *last;
            return ret;
        }
        *head = cons(root, obj, head); // 把讀到的 obj 接到 head 頭端
    }
}

// May create a new symbol. If there's a symbol with the same name, it will not create a new symbol
// but return the existing one.
static Obj *intern(void *root, char *name) { // 如果 name 變數存在，就傳回該變數，否則創建 name 為新變數
    for (Obj *p = Symbols; p != Nil; p = p->cdr)
        if (strcmp(name, p->car->name) == 0)
            return p->car;
    DEFINE1(sym);
    *sym = make_symbol(root, name);
    Symbols = cons(root, sym, &Symbols);
    return *sym;
}

// Reader marcro ' (single quote). It reads an expression and returns (quote <expr>).
static Obj *read_quote(void *root) { // 讀取 'expr 後創建 (quote expr) list
    DEFINE2(sym, tmp);
    *sym = intern(root, "quote");
    *tmp = read_expr(root);
    *tmp = cons(root, tmp, &Nil); // tmp = (expr nil) 
    *tmp = cons(root, sym, tmp);  // tmp = (quote (expr nil))
    return *tmp;
}

static int read_number(int val) { // 讀入一個數值
    while (isdigit(peek()))
        val = val * 10 + (getchar() - '0');
    return val;
}

static Obj *read_symbol(void *root, char c) { // 讀入一個符號
    char buf[SYMBOL_MAX_LEN + 1];
    buf[0] = c;
    int len = 1;
    while (isalnum(peek()) || strchr(symbol_chars, peek())) {
        if (SYMBOL_MAX_LEN <= len)
            error("Symbol name too long");
        buf[len++] = getchar();
    }
    buf[len] = '\0';
    return intern(root, buf);
}

static Obj *read_expr(void *root) { // 讀取一個 expr = (...)
    for (;;) {
        int c = getchar();
        if (c == ' ' || c == '\n' || c == '\r' || c == '\t')
            continue;
        if (c == EOF)
            return NULL;
        if (c == ';') {
            skip_line();
            continue;
        }
        if (c == '(')
            return read_list(root);
        if (c == ')')
            return Cparen;
        if (c == '.')
            return Dot;
        if (c == '\'')
            return read_quote(root);
        if (isdigit(c)) // 數值
            return make_int(root, read_number(c - '0'));
        if (c == '-' && isdigit(peek())) // 負號開頭的數值
            return make_int(root, -read_number(0));
        if (isalpha(c) || strchr(symbol_chars, c)) // symbol 名稱符號
            return read_symbol(root, c);
        error("Don't know how to handle %c", c);
    }
}
