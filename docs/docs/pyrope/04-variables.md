# Variables and types

A variable is an instance of a given type. The type may be inferred from use.
The basic types are Number, String, Boolean, Functions, and Bundles.  All the
types are build around these basic types.


## Mutable/Immutable

Variables are immutable by default and bundle fields are mutable by default.
There are 3 keywords to handle mutability:

* `var` is used to declare mutable variables.
* `let` is used to declare immutable variables.
* `mut` is used to modify mutable variables. The `mut` keyword is not needed when there is a op= assignment.
* `set` is used to potentially add new fields to a mutable bundle.

```
a  = 3
a  = 4         // compile error, 'a' is immutable
a += 1         // compile error, 'a' is immutable

var b  = 3
mut b  = 5     // OK
    b  = 5     // compile error, 'b' is already declared as mutable
    b += 1     // OK, OP= assumes mutable
mut b += 1     // OK, mut is not needed in this case

var c=(x=1,let b=2, mut d=3) // mut d is redundant
mut c.x   = 3  // OK
mut x.foo = 2  // compile error, bundle 'x' does not have field 'foo'
set x.foo = 3  // OK
mut c.b   = 10 // compile error, 'c.b' is immutable

let d=(x=1, let y=2)
mut d.x   = 2  // OK
set d.foo = 3  // compile error, bundle 'd' is immutable
mut d.y   = 4  // compile error, 'd.y' is immutable
```

Bundles can be mutable. This means that fields and subfields can be added with
successive statements.

```
var a.foo = (a=1,b=2)
mut a.bar = 3
set a.foo ++= (c=4)
assert a.foo.c == 4
```

## Variable modifiers

The first character[s] in the variable modify/indicate the behavior:

* `$`: for inputs, all the inputs are immutable. E.g: `$inp`
* `%`: for outputs, all the outputs are mutable. E.g: `%out`
* `#`: for registers, all the registers are mutable. E.g: `#reg`
* `_`: for private variables. It is a recommendation, not enforced by the compiler.

```
%out = #counter
if $enable {
  #counter = (#counter + 1) & 0xFF
}
```

## comptime

Pyrope borrows the `comptime` keyword and functionality from Zig. Variables, or expressions,
can be declared compile time constants or `comptime`. This means that the value must be 
constant at compile time or an error is generated.

```
let comptime a = 1     // obviously comptime
var comptime b = a + 2 // OK too
let comptime c = $rand // compile error, 'c' can not be computed at compile time
```

The `comptime` directive considers values propagated across modules.

## debug

In software and more commonly in hardware, it is common to have extra statements
to debug the code. These statements can be more than plain assertions, they can also
include code.

The `debug` attribute marks a mutable or immutable variable. At synthesis, all
the statements that use a `debug` can be removed. `debug` variables can read
from non debug variables, but non-debug variables can not read from `debug`.
This guarantees that `debug` variables, or statements, do not have any
side-effect beyond debug statements.

```
var a = (debug b=2, c = 3) // a.b is a debug variable
let debug c = 3
```

## Basic type annotations

Global type inference and unlimited precision allows to avoid most of the
types. Pyrope allows to declare types. The types have two main uses, they
behave like assertions, and they allow function polymorphism.

```
var a:u120    // a is an unsigned value with up to 120bits, initialized to zero

var x:s3 = 0  // x is a signed value with 3 bits (-4 to 3)
mut x = 3     // OK
mut x = 4     // compile error, '4' overflows the maximum allowed value of 'x'

var person = (
  ,name:string // empty string by default
  ,age:u8      // zero by default
)

var b
b ++= (1,2)
b ++= (3,4)

assert b == (1,2,3,4)
```

The basic type keywords provided by Pyrope:

* `boolean`: true or false boolean. It can not be undefined (`0sb?`).
* `string`: a string.
* `{||}`: is a function without any statement which can be used as function type.
* `unsigned`: an unlimited precision natural number.
* `u<num>`: a natural number with a maximum value of $2^{\texttt{num}}$. E.g: `u10` can go from zero to 1024.
* `int`: an unlimited precision integer number.
* `i<num>`: an integer 2s complement number with a maximum value of $2^{\texttt{num}-1}-1$ and a minimum of $-2^{\texttt{num}}$.


Each bundle is has a type, either implicit or explicit, and as such it can be
used to declared a new type. The `type` keywords guarantees that a variable is
just a type and not an instance.

```
var bund1 = (color:string, value:s33)
var x:bund1          // OK
bund1.color = "red"  // OK
x.color     = "blue" // OK

type typ = (color:string, value:s20)
var y:typ            // OK
typ.color = "red"    // compile errro
y.color   = "red"    // OK
```

## Operators

There are the typical basic operators found in most common languages with the
exception exponent operations. The reason is that those are very hardware
intensive and a library code should be used instead.

All the operators work over signed integers.

### Unary operators

* `!` or `not` logical negation
* `~` bitwise negation
* `-` arithmetic negation

### Binary operators

* `+` addition
* `-` substraction
* `*` multiplication
* `/` division
* `and` logical and
* `or` logical or
* `implies` logical implication
* `&` bitwise and
* `|` bitwise or
* `^` bitwise or
* `>>` shift right
* `<<` shift left

Most operations behave as expected when applied to signed unlimited precision integers. Logical
and arithmetic operations can not be mixed.

```
x = a and b
y = x + 1    // compile error: 'x' is a boolean, '1' is not
```

### Reduce and bit selection operators

The reduce operators and bit selection share a common syntax `@op[selection]`
where there can be different operators (op) and/or bit selection.

The valid operators:
* `|`: or-reduce.
* `&`: and-reduce.
* `^`: xor-reduce or parity check.
* `+`: pop-count.
* `sext`: Sign extend select bits.
* `zext`: Zero sign extend select bits.

If no operator is provided, a `zext` is used. The bit selection without
operator can also be used on the left hand side to update a set of bits.
The bit selector.

The or/and/xor reduce have a single bit signed output (not boolean). This means
that the result can be 0 (`0sb0`) or -1 (`0sb1`).

```
x = 0b10110
y = 0s10110
assert x@[0,2] == 0b10
assert y@[100,200]     == 0b11 and x@[100,200]     == 0
assert y@sext[100,200] ==   -1 and x@sext[100,200] == 0
assert x@|[] == -1 
assert x@&[0,1] == 0
assert x@+[] == 3 and y@+[] == 3

var z     = 0b0110
mut z@[0] = 1    // same as mut z@[0] = -1 
assert z == 0b0111
mut z@[0] = 0b11 // compile error, '0b11` overflows the maximum allowed value of `z@[0]`
```

### Operator with bundles

There are some operators that can also have bundles as input and/or outputs.

* `++` concatenate two bundles
* `<<` shift left. The bundle can be in the right hand side
* `has` checks if a bundle has a field.

The `<<` allows to have multiple values provided by a bundle on the right hand side or amount. This is useful
to create one-hot encodings.

```
y = (a=1,b=2) ++ (c=3)
assert y == (a=1,b=2,c=3)
assert y has 'a' and y has 'c'

x = 1<<(1,4,3)
assert x == 0b01_1010
```

## Precedence

Pyrope has a very shallow precedence, unlike most other languages the
programmer should explicitly indicate the precedence. The exception is for
widely expected precedence.

* Unary operators (not,!,~,?) bind stronger than binary operators (+,++,-,*...)
* Always left-to-right evaluation.
* Comparators can be chained (a==c<=d) same as (a==c and c<=d)
* mult/div precedence is only against +,- operators.
* Parenthesis can be avoided when a expression only has variables (no function
  calls) and the left-to-right has the same result as right-to-left.

| Priority | Category | Main operators in category |
|:-----------:|:-----------:|-------------:|
| 1          | unary       | not ! ~ ? |
| 2          | mult/div    | *, /         |
| 3          | other binary | ..,^, &, -,+, ++, --, <<, >> |
| 4          | comparators |    <, <=, ==, !=, >=, > |
| 5          | logical     | and, or, implies |


To reduce the number of parenthesis and increase the visual/structural
organization, the newlines behave like inserting a parenthesis after the
statement and the end of the line.

```
assert (x or !y) == (x or (!y) == (x or not y)
assert (3*5+5) == ((3*5) + 5) == 3*5 + 5

a = x1 or x2==x3 // same as b = x1 or (x2==x3)
b = 3 & 4 * 4    // compile error: use explicit precendence between '&' and '*'
c = 3
  & 4 * 4
  & 5 + 3        // OK, same as b = 3 & (4*4) & (5+3)
d = 3 + 3 - 5    // OK, same result right-left

// e = 1 | (5) & (6) // precendence problem even with newlines
e = 1
  | 5
  & 6           // compile error: use explicit precendence between '&' and '|'

f = 1 & 4
  | 1 + 5
  | 1           // OK, same as (1&4) | (1+5) | (1)

g = 1 + 3
  * 1 + 2
  + 5           // OK, same as (1+3) * (1+2) + (5)

h = x or y and z// compile error: use explicit precedence between 'or' and 'and'

i = a == 3 <= b == d
assert i == (a==3 and 3<=b and b == d)
```

