# Basics

## Comments

Comments begin with `//`, there are no multi-line comments

```
// comment
a = 3 // another comment
```

## Constants

Pyrope has 3 basic constants:

* `number`: which is signed integer of unlimited precision
* `string`: which is a sequence of characters
* `boolean`: either `true` or `false`

### Numbers or integers

Pyrope has unlimited precision signed integers.

```
0xF_a_0 // 4000 in hexa. Underscores have no meaning
0b1100  // 12 in binary
0sb1110 // -2 in binary (sb signed binary)
33      // 33 in decimal
0o111   // 73 in octal
0111    // 111 in decimal (some languages use octal here)
```

Since powers of two are very common, Pyrope decimal numbers have the `k`, `m`, `g` modifiers.

```
assert 1k == 1K == 1024
assert 1m == 1M == 1024*1024
assert 1g == 1G == 1024*1024*1024
```

Several hardware languages support unknown bits (`?`) or high-impedance (`z`). Pyrope
aims at being compatible with synthesizable Verilog, as such such values are also supported in
the binary encoding.

```
0b?    // 0 or 1 in decimal
0sb?   // 0 or -1 in decimal
0b?0   // 0 or 2 in decimal
0sb0?0 // 0 or 2 in decimal
0b0?_1??0z_??z0 // valid value
```

Like in many HDLs, Pyrope has unknowns `?`. The x-propagation is a source of complexity in most hardware models. Pyrope has x or
`?` to be compatible with Verilog existing designs. The advise is not to use x besides `match` statement pattern matching. It is
much better to use the default value (zero or empty string), but sometimes it is easier to use `nil` when converting Verilog code
to Pyrope code. The `nil` means that the numeric value is invalid. If any operation is performed with nil, the result is an
assertion failure. The only thing allowed to do with nil is to copy it. While the `nil` behaves like an invalid value, the `0sb?`
behaves like an unknown value that still can be used in arithmetic operations. 

Notice that `nil` is a state in the Number basic type, it is not a new type by itself, it does not represent invalid pointer, but 
rather invalid number.

```
a = 0sb? & 0 // OK, result is 0
b = nil  & 0  // Error
```

### Strings

Pyrope accepts single line strings with single quote (`'`) or double quote
(`"`).  Single quote only has `\'` as escape character. double quote supports
extra escape sequences.

```
a = "hello \n newline"
b = 'simpler here'
```

* `\n`: newline
* `\\`: backslash
* `\"`: double quote
* `\'`: single quote (only one allowed in single quote)
* `\xNN`: hexadecimal 8 bit character (2 digits)
* `\uNNNN`: hexadecimal 16-bit Unicode character UTF-8 encoded (4 digits)


Numbers and strings can be converted back and forth:

```
a = "127"
b = a.__to_i()
c = a.__to_s()
assert a == c
assert b == 0x7F
```

A Pyrope std library could provide a better interface in the future like `a.to_i()`, but fields that start with double underscore
are reserved to interact with the LiveHD compiler or call the C++ provided library.

### Unique identifiers

When an identifier uses an all upper case (E.g: `ALL_CAPS`). Pyrope assigns a
unique identifer for each upper case constant. The value is unique and not
visible, but it can be used to index tuples or to compare equality. The
identifier scope is the whole Pyrope file.

```
a = ONE
b = TWO
assert a!=b
val[ONE] = true
```

## newlines and spaces


Spaces do not have meaning but new lines do. Several programming languages like Python use indentation level (spaces) to know the
parsing meaning of expressions. In Pyrope, spaces do not have meaning, but new lines affect the operator precedence and multi line
statements.


By looking at the first character after a new line, it is possible to know if the rest of the line belongs to the previous
statement or it is a new statement.

If the line starts with a alphanumeric (`[a-z0-9]`) value or an open parenthesis (`(`), the rest of the line belongs to a new
statement.

```
a = 1
  + 3          // 1st stmt
(b,c) = (1,3)  // 2nd stmt
d = 1 +
    3          // compile error
```

This functionality allows to parallelize the parsing and elaboration in Pyrope.  Also, makes the code more readable. Avoiding
multi-line comments is always a good idea.


Pyrope has a very restricted/shallow operator precedence that forces to use parenthesis more frequently than other languages.  The
new line is a visual break that behaves like adding a open/close parenthesis after the first operator and the end of the line.

```
a = 1 - 4  // same as: a = (1 - 4)
  * 1 + 4  // same as:   * (1 + 4)
  * 2 * 3  // same as:   * (2 * 3)
```

### Identifiers


An identifier is any non reserved keyword that starts with an underscore or an alphabetic character.
Since Pyrope is designer to support any synthesizable Verilog automatic translation, the any sequence of characters between \` can
form a valid identifier. This is needed because Verilog has the \\ that builds identifiers with special characters.

```
`foo is . strange!` = 4
```

## Semicolons

Semicolons are not needed to separate statements. In Pyrope, a semicolon (`;`)
has exactly the same meaning as a newline. Sometimes it is possible to add
semicolons to separate statements. Since newlines affect the meaning of the
program, a semicolon can do too.

```
a = 1 ; b = 2
```

## Printing

Printing messages is useful for debugging. `puts` prints a message and the string
is formatted using the c++20 fmt format. There is an implicit new line printed.
The same without a newline can be achieved with print.

```
a = 1
puts "Hello a is {}", a
```

Since many modules can print at the same cycle, it is possible to put a relative
order between puts (`order`).

This example will print "hello world" even though there are 2 puts/prints in different files.

```
// src/file1.prp
puts order=2, " world"
// src/file2.prp
print order=1, "hello"
```

The available puts/print arguments:
* `order`: relative order to print in a given cycle
* `file`: file to send the message. E.g: `stdout`, `my_large.log`,...

