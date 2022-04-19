# minilisp

## 用法 (Linux/WSL 中)

```
$ wsl
wsl> make clean
rm -f minilisp *~
wsl> make
cc -std=gnu99 -g -O2 -Wall    minilisp.c   -o minilisp
wsl> ./minilisp
(+ 2 3)
5
'a
a
(quote a)
a
'63
63
(- 3)
-3
(- 3 5)
-2
(- 3 5 7)
-9
(< 2 3)
t
(< 3 3)
()
(< 4 3)
()
(a b c)
Undefined symbol: a
wsl> '(a b c)
> ^C
wsl> ./minilisp
'(a b c)
(a b c)
'(a b . c)
(a b . c)
(cons 'a 'b)
(a . b)
(cons 'a (cons 'b ())     
)
(a b)
(cons 'a 'b)
(a . b)
(car '(a b c))
a
(cdr '(a b c))
(b c)
(define obj (cons 'a 'b)) (setcar obj 'x) obj
(a . b)
(x . b)
(x . b)
(define x 7) (+ x 3)
7
10
```

## 執行程式

```
v
```