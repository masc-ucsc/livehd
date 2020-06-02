06/01/2020
Question: Hi Jose, Do you agree that the dp_assign should only be used only
after the bitwidth of a variable is explicitly and strictly defined?

```
  // case I -> compile error
  x = 3
  x := x + 1 //no constrains on a, why use dp_assign here?? -> error
  // cast II-> compile error
  x.__bits = 2 // sets bits = 2, but could be automatically extended by the following computation
  x = 3
  x := x + 1   // makes no sense as x.__bits is not strictly constrained.
  // case III-> compiles
  x.__bits as 2 // explicitly fix BW using "as"
  x = 3
  x := x + 1    // makes sense to use dp_assin as x is constrained to bits 2
  I(x == 0)
  // case IV-> compiles
  x = 3u2bits  // explicitly fix BW when constant declaration
  x := x + 1   // makes sense to use dp_assin as x is constrained to bits 2
  I(x == 0)
```

Ans: Let me comment the code:

```  
  // case I -> NO compile error
  x = 3      // bitwidth finds that x.__bits = 2
  x := x + 1 // no error: run time drops upper bit x[0:1] gets x[0:1] + 1 -> x becomes 0
  I(x==0)
  // cast II-> NO compile error
  x.__bits = 2 // sets bits = 2, but could be automatically extended by the following computation
  x = 3
  x := x + 1   // same as case I, x becomes 0 after overflow drop
  I(x==0)
  // case III-> compiles
  x.__bits as 2 // explicitly fix BW using "as"
  x = 3
  x := x + 1    //
  I(x == 0)
  // case IV-> compiles
  x = 3u2bits  // explicitly fix BW when constant declaration
  x := x + 1   // makes sense to use dp_assin as x is constrained to bits 2
  I(x == 0)
 ``` 


  In your examples, all of them compile and return x==0. There is a confusion with
  ```
    x.__bits = 2 vs x.__bits as 2
  ```

  The = just means that \_\_bits is set, NO assignment can overflow it (same with
  "as"), but the \_\_bits field can be changed.
  ```
    x.__bits = 3
    y.__bits as 3
    x = 30 // compile error
    y = 30 // compile error
    x = 1u4 // compile error
    y = 1u4 // compile error
    x.__bits = 5 // OK
    y.__bits as 5 // compile error. Already fixed
  ```

  The bitwidth pass does not change because "as" vs "=" in the \_\_bits assignment
  field.  
  -> The ":=" semantics: The lhs bitwidth does not change as a result of
  the operation. (whatever lhs with was set/inferred before is kept)
  
  -> The "=" semantics
  The lhs bitwidth is inferred (sometimes conservatively) to guarantee no bit drop
  
  -> The "as" semantics
  Same semantics as "=" but it also forces the variable contents as final (it
  can not be changed after the assignment) 

  The ":=" means do not bitwidth lhs, so it is very difficult to result in a
  compile error (overflow is dropped, underflow is zero/sign-extend
  concatenated), but it can easily introduce a bug.

Question: 
Hi Jose,
Thanks for your explanation.
I just want to confirm with you with the following statement:
```
  The "=" semantics
  The lhs bitwidth is inferred (sometimes conservatively) to guarantee no bit drop
```

The lhs bitwidth is only inferred when it's not explicitly set, for example
```
  $a.__bits = 2
  $b.__bits = 1
  %o = $a + $b  //%o should automatically inferred as bits 3
```
once the bitwidth is explicitly assigned for a variable, the "=" semantics
actually doesn't allow any inferred bitwidth

```
  x.__bits = 2
  x = 5 //compile error, 2bits <- 3bits
```

Ans: Yes

