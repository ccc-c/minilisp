# cli

```
wsl> ./minilisp
(println 3) 
3
()
(println '(hello world))
(hello world)
()
(define a (+ 1 2))
3
(+ a a)
6
(define double (lambda (x) (+ x x)))
<function>
(double 6)
12
((lambda (x) (+ x x)) 6)
12
(defun double (x) (+ x x))
<function>
(double 6)
12
(defun fn (expr . rest) rest)
<function>
(fn 1)
()
(fn 1 2 3)
(2 3)

(define counter
  ((lambda (count)
     (lambda ()
       (setq count (+ count 1))
       count))
   0))
<function>
(counter) 
1
(counter) 
2
(counter) 
3
(define val (+ 3 5))
8
(setq val (+ val 1))
9
```