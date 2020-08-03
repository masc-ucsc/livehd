
Test cases with issues:




1-method call for ..foo.. syntax (correctness)

This is a code example
```
%out = a ..concat.. b

// equivalent to say %out = concat(a,b)
````

The current prp_lnast:
```
./bazel-bin/inou/pyrope/prp_lnast_test ./pp5.prp

Parsing SUCCESSFUL!
prp_lnast_test.parse secs=0.000519139:IPC=1.1472:BR MPKI=2.1555:L2 MPKI=27.1027
prp_last_test.convert secs=0.00023993:IPC=1.04265:BR MPKI=1.94184:L2 MPKI=27.2821
AST to LNAST output:

0                     top :
1                statements : ___SEQ0
2                      assign :
3                           ref : %out
3                           ref : a
```

It does not have the function call.

The current prp (pegjs) has:

```
1       0       0       SEQ0
2       1       0       1       22      ..concat..      ___a    a       b
3       1       1       1       22      =       %out    ___a
```


2-Extra unnecessary or-nodes (optimization, not correctness)

To optimize this assign case:

```
a = 1 + 2
```

It creates 2 lnast nodes, an "add" and an "assign". The assign is not really needed:

```
0                     top :
1                statements : ___SEQ0
2                        plus :
3                           ref : ___a
3                         const : 2
3                         const : 1
2                      assign :
3                           ref : a
3                           ref : ___a
```

It would be better if we can do this:

```
0                     top :
1                statements : ___SEQ0
2                        plus :
3                           ref : a
3                         const : 2
3                         const : 1
```

Most of the "assign" statements (not the := or "as") could be removed.
```
2                      assign :
3                           ref : a
3                           ref : ___a
```


3-incorrect arguments in fcall

This example
```
a |> fcall(c,d)
```

Incorrectly generates:
```
2                       tuple :
3                           ref : ___a
3                           ref : c
3                           ref : d
3                           ref : a
2                   func_call :
3                           ref : ___b
3                           ref : fcall
3                           ref : ___a
```

It should be:

```
2                       tuple :
3                           ref : ___a
3                           ref : c
3                           ref : d
2                tuple_concat :
3                           ref : ___b
3                           ref : a
3                           ref : ___a
2                   func_call :
3                           ref : ___c
3                           ref : fcall
3                           ref : ___b
```

There is a connected/correlated problem. The following code should generate the
same code as the previous snipped (with the exception of the x created
variable)
```
x = a ++ (c,d)
x|>fcall
```

But it generates:
```
2                       tuple :
3                           ref : ___a
3                           ref : c
3                           ref : d
2                tuple_concat :
3                           ref : ___b
3                           ref : a
3                           ref : ___a
2                      assign :
3                           ref : x
3                           ref : ___b
2                       tuple :                 <--- this tuple node should not exist when calling a |> fcall, just for fcall(a)
3                           ref : ___d
3                           ref : x
2                   func_call :
3                           ref : ___c
3                           ref : fcall
3                           ref : ___d
```

This example works correctly, (even saves the concat which is great)
```
(a,b) |> fcall(c,d)
```

It generates:
```
2                       tuple :
3                           ref : ___a
3                           ref : c
3                           ref : d
3                           ref : a
3                           ref : b
2                   func_call :
3                           ref : ___b
3                           ref : fcall
3                           ref : ___a
```

