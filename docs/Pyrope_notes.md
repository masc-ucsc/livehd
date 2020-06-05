06/01/2020
#Pyrope dp_assign operator
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

=======================================================================================

05/14/2020
# Pyrope shift operators

Jose: 
a>>b is a shift right.
We do not have a clear way to distinguish between arithmetic and logical shift. In C/C++ the >> is always arithmetic. We should assume the same for Pyrope.
The question is "do we have any for logical shift in pyrope/LNAST"?

Jose: 
let me summarize:
```
Pyrope:
c= a>>b  // arithmetic shift c.__bits = a.__bits-b ; c.__sign = a.__sign
c= a>>>b // rotate c.__bits = a.__bits ; c.__sign = a.__sign??
c= a[[b..]] // c.__bits = a.__bits - b ; c.__sign = false
```
Sheng-Hong:
I would like to further discuss the shift ops in "LGraph",
currently, the following types are in LGraph.
```
1. logic shift right/left
2. arith shift right/left
3. dynamic shift right/left
4. shift right/left
```
I think items 1 & 2 should be deprecated and we could use item 4 only. Or vise
versa the  "4. shift right/left" could be covered by items 1 & 2, and it should
be deprecated.  And for the "3. dynamic shift right/left", if mockturtle is only
the user, we should also deprecate it. Because the current mockturtle code
doesn't implement it correctly and detailly, either.  and in the future, we
should add
```
5. rotate shift right/left  //todo
```
What do you think?

Hunter:
FIRRTL has shift_left, shift_right (this always uses the signed bit),
dynamic_shift_left, and dynamic_shift_right. For shift_right, if the value is
signed it is always arithmetic. If the value is not signed it is always logical.


Jose:
OK, Yosys also only uses ShiftRight_Op and ShiftLeft_Op. I think that those are
the only ones that we should keep in LG The only issue is "how to detect" that
the input is signed/unsigned. I guess that we need to track it like the bits.
Now, this info is just in bitwidth pass.
