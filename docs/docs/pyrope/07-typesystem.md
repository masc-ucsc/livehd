# Type system

Type systems are quite similar to sets. A main difference is that type systems
may not be as accurate as a set system, and it may not allow the same
expressiveness because some type of set properties may not be allowed to be
specified. 


Most HDLs do not have modern type systems, but they could benefit like in other
software domains. Additionally, in hardware it makes sense to have different
implementations that adjust for performance/constrains like size, area,
FPGA/ASIC. Type systems could help on these areas.


## Types vs `comptime assert`

Pyrope has support for different types of assertions (`assert`, `comptime
assert`, `assume`, `comptime assume`, `verify`).  The type system checks, not
the function overloading, can be translated to a set of `comptime assert`
statements. Pyrope type checks can be translated to compile time assertion
checks, but the type related language syntax makes it more readable/familiar
with programmers.


To understand the type check, it is useful to see an equivalent `comptime assert`
translation. Each variable can have a type attached once. Each time that the
variable is modified a `comptime assert` statement could check that the variable is
compatible with the assigned type. From a practical perspective, the Pyrope
type system works this way when variables are modified.

=== "Snippet with types"

    ```
    var b = "hello"

    var a:u32

    mut a += 1

    mut a = b // fails type check
    ```

=== "Snippet with comptime assert"

    ```
    var b = "hello"



    mut a += 1
    comptime assert a does u32
    mut a = b 
    comptime assert a does u32 // fails type check
    ```

The compiler handles automatically, but control flow instructions affect the
equivalent assert statement.

```
var a:type1

if $runtime {
  var b:type2

  a = yyy      // comptime assert $runtime implies yyy does :type1
  b = xxx      // comptime assert $runtime implies xxx does :type2
}

a = zzz        // comptime assert zzz does :type1
```

## Building types

Each variable can be a basic type like String, Boolean, Number, or a bundle. In
addition, each variable can have a set of constrains from the type system. 


Although it is possible to declare just the `comptime assert` for type checks,
the recommendation is to use the explicit Pyrope type syntax because it is more
readable and easier to optimize.


Pyrope type constructs:

* `type` keyword allows to declare types.
* `a does b`: Checks 'a' is a superset or equal to 'b'. In the future, the
  unicode character "\u02287" could be used as an alternative to `does` (`a`
&#8839 `b`);
* `a:b` is equivalent to `a does b` or `comptime assert a does b` check.
* `:b` returns the "type of" `b` when used in an expression.
* `a equals b`: Checks that `a does b` and `b does a`. Effectively checking
  that they have the same type.


While `var` statement declares a new variable instance which can also have an
associated type, the `type` statement declares a type without any instance.
The `type` keyword also allows for expressions to build more complex types.
All the elements in the type expression are treated as "type of". E.g: `type x
= a or 1..=3` is equivalent to write `type x = :a or :(1..=3)` 

```
type a1 = u32       // type a1 = :u32 is also valid syntax
type a2 = int(max=33,min=-5)
type a3 = (
    ,name:string
    ,age:u8
    )

type b1 = a1 or  a2 // same as type b1 = -5..<4G
type b2 = a1 and a2 // same as type b2 = 0..=33

type b3 = a1 or a3  // compile error: unclear how to combine type 'a1' and 'a2'
```

The puts command understands types.

```
type at=33..   // number bigger than 32
type bt=(
  ,c:string
  ,d=100
  ,initial = {|| self.c = $ }
)

var a:at=40
var v:bt="hello"
puts "a:{} type:{} or {}", a, :a, at  // a:40 type:Number(33..) or Number(33..)
puts "b:{} type:{}", b, :b  // b:(c="hello",d=100) type:(c:string,d=100)"
```


Some languages use an `is` keyword but Pyrope uses `does` or `equals` because
in English "a is b" is not clear ("a is same as b" vs "a is subtype of b"). 

```
type x = (a:string, b:int)
type y = (a:string)
type z = (a:string, b:u32, c:i8)

assert   x does y
assert   y does y
assert   z does y
assert !(x does z)
assert !(y does z)
assert !(y does x)
assert !(z does x)

type big = x or y or z or :(d:u33)
assert   big does x
assert   big does y
assert   big does z
assert   big does :(d:u20)
assert !(big does :(d:u40))
```

## Enums with types

The union of types is the way to implement enums in Pyrope:

```
 type color = RED or BLUE or GREEN // enum just a unique ID

 type Rgb = (
    ,color:u24
    ,initial = {|x| self.color = x }
 )

 type Red   = Rgb(0x0xff0000)
 type Green = Rgb(0x0x00ff00)
 type Blue  = Rgb(0x0x0000ff)
 type color2 = Red or Green or Blue

 var x:color = RED // only in local module

 if x does RED {   // in this case "x does RED" is the same as "x equals RED"
   puts "color:{}\n", :x // prints "color:RED"
 }

 var y:color2 = Red
 if y does Red { // in this case "y does RED" is the same as "y equals RED"
   // prints "color:Red c1:Red(color=0xff0000) c2:0xff0000"
   puts "color:{} c1:{} c2:{}\n", :y, y, y.color 
 }
```


## Bitwidth for numbers

Number basic type can be constrained based on the maximum and minimum value
(not by number of bits).

Pyrope automatically infers the maximum and minimum value for each numeric
variable. If a variable width can not be inferred, the compiler generates a
compilation error. A compilation error is generated if the destination
variable has an assigned size smaller than the operand results. 

The programmer can specify the maximum number of bits, or the maximum value range.
The programmer can not specify the exact number of bits because the compiler has
the option to optimize the design.

Pyrope code can set or access the bitwidth pass results for each variable.

* `__max`: the maximum number
* `__min`: the minimum number
* `__sbits`: the number of bits to represent the value
* `__ubits`: the number of bits. The variable must be always positive or a compile error.

```
var val:u8 // designer constraints a to be between 0 and 255
val = 3    // val has 3 bits (0sb011 all the numbers are signed)

val = 300  // compile error, '300' overflows the maximum allowed value of 'val'

val = 0x1F0@[0..<val.__ubits] // explicitly select bits to not overflow
assert val == 240

val := 0x1F0       // Drop bits from 0x1F0 to fit in maximum 'val' allowed bits
assert val == 240

val = u8(0x1F0)    // Adjust val to the maximum value if overflow
assert val == 255

val = :val(0x1F0)  // Adjust val to the maximum value if overflow
assert val == 255
```

Pyrope leverages LiveHD bitwidth pass [stephenson_bitwidth] to compute the maximum and minimum
value of each variable. For each operation, the maximum and minimum is computed. For control
flow divergences, the worst possible path is considered.

```
a = 3                      // max:3, min:3
if b {
  c = a+1                  // max:4, min:4
}else{
  c = a                    // max:3, min:3
}
e.__sbits = 4              // max:3, min:-4
e = 3                      // max:3, min:3
d = c                      // max:4, min:3
if d==4 {
  d = e + 1                // max:4, min:4
}
g = d                      // max:4, min:3
h = c@[0,1]                // max:3, min:0
```

## Typecasting


Typecasting is the process of changing from one type to other. The Number/int
type  allows to specify the maximum/minimum value per bit, this is not
considered a new type.  Since bitwidth pass adjust/computes the maximum/minimum
range for each Number type, as long as precision is not lost, type casting
between Numbers is done automatically.


When the precision can not be preserved in a Number, a `:=` or a typecast could
be used.  The `lhs := rhs` statement drops the bits in the `rhs` to fit on the
`lhs`. An alternative method to typecase is to call the constructor, for the Number
class, this does not drop bits, but keeps the maximum/minimum allowed value.

```
var a:u32=100
var b:u10
var c:u5
var d:u5

b = a     // OK done automatically. No precision lost
c = a     // compile error, '100' overflows the maximum allowed value of 'c'
c:= a     // OK, same as c = a@[0..<5] (Since 100 is 0b1100100, c==4)
c = u5(a) // OK, c == 31
c = 31
d = c + 1 // compile error, '32' overflows the maximum allowed value  of 'd'
d:= c + 1   // OK d == 0
d = u5(c+1) // OK, d==31
d = :d(c+1) // OK, d==31
```

To convert between bundles, a explicit typecast is needed unless all the bundle
fields match and field can be automatically typecasted without loss of precision.

```
type at=(c:string,d:u32)
type bt=(c:string:d:u100)
type ct=(
  ,d:u32
  ,c:string
  ) // different order
type dt=(
  ,d:u32
  ,c:string
  ,initial = {|x:at| self.d = x.d ; self.c = x.c }
  ) // different order

var b:bt=(c="hello", d=10000)
var a:at

a = b // OK c is string, and 10000 fits in u32

var c:ct
c = a // compile error, different order

var d:dt
d = a // OK, call intitial to type cast
```


## Traits and mixins

There is no object inheritance in Pyrope, but bundles allow to build mixins and composition with traits.

A mixin is when an object or class can add methods and the parent object can access them. In several languages
there are different constructs to build them (E.g: an include inside a class in Ruby). Since Pyrope bundles
are not immutable, new methods can be added like in mixins.

```
type Say_mixin = (
  ,say = {|s| puts s }
)

type Say_hi_mixin = (
  ,say_hi  = {|| self.say("hi {}", self.name)
  ,say_bye = {|| self.say("bye {}", self.name)
)

type User = (
  ,name:string
  ,initial = {mut |n| self.name = n }
)

type Mixing_all = Say_mixin ++ Say_hi_mixin ++ User

var a:Mixing_all("Julius Caesar")
a.say_hi() 
```

Mixins are very expressive but allow to redefine methods. If two bundles have
the same field a bundle with the concatenated values will be created. This is
likely an error with basic types but useful to handle explicit method overload.
This could be error prone and in many cases it may be fine just to use the
trait construction. The `implements` keyword checks that the new type
implements the functionality undefined and allows to use methods defined. This
is effectively a mixin with checks that some methods should be implemented.

```
type Shape = (
  ,name:string
  ,area          = {   |(     ) -> :i32 |}
  ,increase_size = {mut|(_:i12) -> ()   |}
)

type Circle implements Shape = (
  ,rad:i32
  ,initial = {mut || self.name = "circle" }
  ,area = {|() -> :i32   |
     let pi = import "math.pi"
     return pi * self.rad * self.rad
  }
  ,increase_size = {mut|(_:i12) -> ()| self.rad *= $1 }
)
```

Like most typechecks, the `implement` can be translated for a `comptime
assert`. An equivalent "Circle" functionality:

```
type Circle = (
  ,rad:i32
  ,name = "Circle"
  ,area = {|() -> :i32|
     let pi = import "math.pi"
     return pi * self.rad * self.rad
  }
  ,increase_size = {mut|(_:i12) -> ()| self.rad *= $1 }
)
comptime assert Circle does Shape
```

## Explicit function overloading

Pyrope has types and functions. There is also function overloading, but unlike
most languages it has explicit function overloading.  With explicit, the
programmer sets an ordered list of methods, and the first that satisfies the
type check is called.

```
bool_to_string = {|(b:boolean) -> :string| if b { "true" } else { "false" } }
int_to_string  = {|(b:int)     -> :string| }

to_string = bool_to_string ++ int_to_string
let s = to_string(3)
```

Liquid types or logically qualified types further constraint some types. In a
way, the maximum/minimum constrain on numbers is already a logically qualified
constrain, but Pyrope allows a `where` keyword when building function types.

Types must be decided at compile time. Some times like in the maximum/minimum
range, the estimation can be conservative. The `where` keyword can use compile
time conditions, but it can also use run-time decisions like values on the
inputs.

When combined with liquid types, it is possible to specialize the functionality
based on targets and/or functionality. Like in the adder example:

```
add_plus_one = {|(a,b) where b == 1 or a == 1|}
fast_csa     = {|(a,b) where min(a.__sbits, b.__sbits)>40|}
default_adder= {|(a,b)|}

my_add = add_plus_one ++ fast_csa ++ default_adder

assert $foo.__sbits < 10   // foo has less than 10 bits
assert $bar.__sbits > 40   // bar has more than 40 bits

result = my_add($foo,$foo) // calls default_adder
result = my_add($bar,$bar) // calls fast_csa
result = my_add($foo,1)    // calls add_plus_one
```

## Global variables

There are no global variables or functions in Pyrope. Variable scope is
restricted by code block `{ ... }` and/or the file. Each Pyrope file is a
function, but they are only visible to the same directory/project Pyrope files.


The `punch` statement allows to access variables from other files/functions. The
`import` statement allows to reference functions from other files.


### import

Each file can have several functions in addition to itself. All the functions
are visible to the `import` statement, but it is a good etiquette not to import
functions that start with underscore, but sometimes it is useful for debugging,
and hence allowed.

```
// file: src/my_fun.prp
fun1    = {|a,b| ... }
fun2    = {|a| ... }
another = {|a| ... }
_fun3   = {|a| ... }
```

```
// file: src/user.prp
a = import "my_fun.*fun*"
a.fun1(a=1,b=2)           // OK
a.another(a=1=2)          // compile error, 'anoter' is not an imported function
a._fun3(a=1=2)            // OK but not nice
```

The import statement uses a shell like file globbing with an optional "project".
If the project is not provided, the current project is used. Globbing is not
allowed on the project name.

* `*` matches zero or more characters
* `?` matches exactly one character

```
a = import "prj1/file?/*something*"
b = import "file1/xxx_fun"   // import xxx_fun from file1 in the local project
c = import "file2"           // import the functions from local file2
d = import "prj2/file3"      // import the functions from project prj2 and file3
```


Many languages have a "using" or "import" or "include" command that includes
all the imported functions/variables to the current scope. Pyrope does not
allow that, but it is possible to use mixins to add the imported functionality
to a bundle.

```
b = import "prp/Number"
a = import "fancy/Number_mixin"

type Number = b ++ a // patch the default Number class

var x:Number = 3
```


### punch

The `punch` statement allows to access variables from other modules. It can be
seen as an `import` but only applicable to read/write variables instead of
functions.  There is another significant difference with `import`, while import
goes through projects and files, `punch` goes through the instantiation
hierarchy to find a matching variable.


The `punch` statement has a regex syntax, not file globbing like in `import`. There can be
many matches for a given regex, it will return all the matches in an ordered bundle.


Given a tree hierarchy, the traversal starts by visiting all the children, then
the parents.  The traversal is similar to a post-order tree traversal, but not
the same. The post-order traversal visits a tree node once all the children are
visited. The `punch` traversal visits a tree node once all the children AND
niblings (niece of nephews from siblings) are visited.


For example, given this tree hierarchy. If the punch is called from 1.2.1 node,
it will visit nodes in this order:

```
            +── 1.2.1.3.1   // 5th
            |── 1.2.1.3.2   // 4th
        +── 1.2.1.1         // 3th
        ├── 1.2.1.2         // 2nd
        |── 1.2.1.3         // 1st
    +── 1.2.1               // START <--
    |   +── 1.3.1.1         // 7th
    |   |── 1.3.1.2         // 8th
    ├── 1.3.1               // 9th
    ├── 1.3.2               // 10th
    ├── 1.3.3               // 11th
    │   -── 1.4.2.1         // 12th
    |   |── 1.4.3.1         // 13th
    ├── 1.4.1               // 14th
    ├── 1.4.2               // 15th
    ├── 1.4.3               // 16th
+── 1.1                     // 17th
├── 1.2                     // 20th
├── 1.3                     // 21st
├── 1.4                     // 22nd
| 1                         // LAST
```

`punch` connects to inputs (`$`), outputs (`%`), and registers (`#`). The
modifier does not need to be included in the search. The regex can include tree
hierarchy. E.g:

```
%a = punch "module1/mod2/foo"

%b = punch "uart_addr" // any module that has an input $uart_addr
%b[0] = 0x100
%b[1] = 0x200

%b = punch "foo.*/uart_addr" // modules named foo.* that have uart_addr as input

$c = punch "bar/some_output"
$d = punch "bar/some_register"
```

The result of the punch has either a `$` or `%`. The reason is that the punch
creates new inputs `$` or outputs `%` in the current module. These do not need
to be in the function declaration list.


