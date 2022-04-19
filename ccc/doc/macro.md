# macro

```
wsl> ./minilisp
(defmacro unless (condition expr)
  ('if condition () expr)                                                  
)
<macro>
(define x 0)
0
(unless (= x 0) '(x is not 0))
The head of a list must be a function
wsl> ./minilisp
(list 'a b c)
Undefined symbol: list
wsl> (defun list (x . y)
-bash: syntax error near unexpected token `('
wsl>   (cons x y))
-bash: syntax error near unexpected token `)'
wsl> ./minilisp
(defun list (x . y)
  (cons x y))
<function>
(defmacro unless (condition expr)
  (list 'if condition () expr))
<macro>
(define x 0)
0
(unless (= x 0) '(x is not 0))
()
(unless (= x 1) '(x is not 1))
(x is not 1)
(macroexpand (unless (= x 1) '(x is not 1)))
(if (= x 1) () (quote (x is not 1)))
(gensym)
G__0
```
