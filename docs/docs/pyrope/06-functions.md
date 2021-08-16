# Functions and methods


Hardware description languages specify a tree-like structure of modules or functions. Usually, there is a top module that
instantiates several sub-modules. The difference between module and function is mostly what is visible/left after synthesis. We
call module any a function call is left visible in the generated netlist as a separate entity. If a function is inlined in the
caller module, we do not call it module. By this definition, a function is a super-set of modules.

!!!Note
    Pyrope only supports a restricted amount of recursion. Recursion is only allowed when it can be unrolled at compile time.

## Function definition

All the functions are lambdas that must passed as arguments or assigned to a given variable. There is no global scope for
variables or functions.

```
just_3 = {   3 } // just scope, not even a lambda is generated
just_4 = {|| 4 } // function that returns 4
```

The simplest function resembles a scope with at `{` followed by a sequence of statements where the last statement can be an
expression before the closing `}`.

The difference between a function and a normal scope is the lambda definition enclosed between pipes (`|`).

```
[ATTRIBUTES] | [META] [CAPTURE] [INPUT] [-> OUTPUT] [where COND] |
```

* ATTRIBUTES are optional method modifiers like:
    * `comptime`: function should be computed at compile time
    * `debug`   : function is for debugging, not side effects in non-debug statements
    * `mut`     : function is a method that can modify variables using `self`.
* META are a list of type identifiers or type definitions.
* CAPTURE has the list of capture variables for the function. If no capture is provided, no variable
can be captured. 
* INPUT has a list of inputs allowed with optional types. If no input is provided, the `$` bundle is used as input.
* OUTPUT has a list of outputs allowed with optional types. If no output is provided, the `%` bundle is used as output.
* COND is the condition under which this statement is valid.

```
add = {|| $a+$b+$c }              // no IO specified
add = {|a,b,c| a+b+c }            // constrain inputs to a,b,c
add = {|(a,b,c)| a+b+c }          // same
add = {|a:u32,b:s3,c| a+b+c }     // constrain some input types
add = {|(a,b,c) -> :u32| a+b+c }  // constrain result to u32
add = {|(a,b,c) -> res| a+b+c }   // constrain result to be named res
add = {|<T>(a:T,b:T,c:T)| a+b+c } // constrain inputs to have same type

x = 2
add2 = {|[x](a)| x + a }           // capture x
add2 = {|[foo=x](a)| foo + a }     // capture x but rename to something else

y = (
  ,val:u32 = 1
  ,inc1 = {mut|| self.val := self.val + 1 } // mut allows to change bundle
)

my_log = {debug||
  print "loging:"
  for i in $ {
    print " {}", i
  }
  puts
}

my_log a, false, x+1
```

## Implicit function per file

Every Pyrope file creates an implicit function with the same name as the file
and visible to the other files/functions in the same directory/project.

Like any function, the input/outputs can be constrained or left to be inferred.


```
// file: src/mycall_with_def.prp
|(a,b) -> (d:u12)|

%d = a + $a + $b // a or $a the same due to function definition
```

```
// file: src/mycall_without_def.prp
%d = $a + $a + $b // a or $a the same due to function definition
assume %d < 4K 
```

## Arguments

Function calls have a bundle as input and another bundle as output. As such,
bundles can be named, ordered or both. `$` is the input bundle, and `%` is the
output bundle. This implies:

* Arguments can be named. E.g: `fcall(a=2,b=3)`
* There can be many return values. E.g: `return (a=3,b=5)`
* Inputs can be accessed with the bundle. E.g: `return $1 + 2`


There are several rules on how to handle function arguments.

* Calls uses the Uniform Function Call Syntax (UFCS). `(a,b).f(x,y) == f((a,b),x,y)`
* Pipe |> concatenated inputs: `(a,b) |> f(x,y) == f(x,y,a,b)`
* No parenthesis after newline or a variable assignment: `a = f(x,y)` is the same as `a = f x,y`


Pyrope uses a uniform function call syntax (UFCS) like other languages like Nim
or D but it can be different from the order in other languages. Notice the
different order in UFCS vs pipe, and also that in pipe the argument tuple is
concatenated, but in UFCS the it is added as first argument.


```
div  = {|a,b| a / $b }     // named input bundle
div2 = {|| $0 / $1 }       // unnamed input bundle

a=div(3  , 4  , 3)         // compile error, div has 2 inputs
b=div(a=8, b=4)            // OK, 2
c=div a=8, b=4             // OK, 2
d=(a=8).div(b=2)           // OK, 4
e=(a=8).div b=2            // compile error, parenthesis is needed for function calls

h=div2(8, 4, 3)            // OK, 2 (3rd arg is not used)
i=8.div2(4,3)              // OK, 2 (3rd arg is not used)

j=(8,4)  |> div2           // OK, 2
k=(4)    |> div2 8         // OK, 2
l=(4,33) |> div2(8)        // OK, 2
m=4      |> div2 8         // OK, 2

n=div2((8,4), 3)           // compile error: (8,4)/3 is undefined
o=(8,4).div2(1)            // compile error: (8,4)/1 is undefined
```

## Methods

A method is a function associated to a bundle. The method names behave like capturing
the bundle in the first argument and adding an extra return for the self object.

```
var a_1 = (
  ,x:u10
  ,fun = {mut |x| 
    assert $.__size == 2 // self and x
    self.a = x 
  }
)

a_1.fun(3)
assert a_1.x == 3

fun2 = {|(x,self)| self.a = x ; return self }
a_2 = a_1.fun2(4)
assert a_1.x == 3
assert a_2.x == 4
```


To access the bundle contents, there are two keywords:

* `self` provides access to the upper level in the bundle.
* `super` provides the method before it was redefined.

```
type base1 = (
  ,fun = {||
    a = super() // nothing after when called
    assert a == nil
    1 
  }
)
type base2 = (
  ,fun = {|| 
    a = super()
    assert a == 1
    2 
  }
)

type top = (
  ,top_fun = {|| 4 }
  ,fun = {||
    a = super()
    assert a == 2
    return 33
  }
) ++ base2 ++ base1

var a3:top

assert a3.top_fun does {||}
assert a3.top_fun.size == 1

assert a3.top_fun does {||}
assert a3.top_fun.size == 1

assert a3.fun does {||}
assert a3.fun.size == 3  // explicit overload
assert a3.fun[0]() == 33
assert a3.fun[1]() == 2
assert a3.fun[2]() == 1
assert a3.fun()    == 33

assert a3.fun() == 33

var a1:base1
var a2:base2
assert a1.fun() == 1
assert a2.fun() == 2
```

