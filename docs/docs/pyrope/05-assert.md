# Assertions

Assertions are consider debug statements. This means that they can not have side effects
on non-debug statements.

Pyrope supports a syntax close to Verilog for assertions. The language is
designed to have 3 levels of assertion checking: simulation runtime,
compilation time, and formal verification time.

There are 4 main methods: 

* `assert`: The condition should be true at runtime. If `comptime assert`, the condition must be true at compile time.
* `assume`: Similar to assert, but allows the tool to simplify code based on it (it has optimization side-effects). 
* `verify`: Similar to assert, but it is potentially slow to check, so checked at runtime or verification step.
* `restrict`: Constraints or restricts beyond to check a subset of the valid
  space. It only affects the verify command. The restrict command accepts a list of conditions to restrict


```
a = 3
assert a == 3          // checked at runtime (or compile time)
comptime assert a == 3 // checked at compile time

verify a < 4           // checked at runtime and verification
assume b > 3           // may optimize and perform a runtime check

restrict foo < 1, foo >3 {
   verify bar == 4  // only checked at verification, restricting conditions
}
```

To guard an assertion for being checked unless some condition happens, you can use the `when/unless` statement modifier
or the `implies` logic. All the verification statements (`assert`, `assume`, `verify`) can have an error message.

```
a = 0
if cond {
  a = 3
}
assert cond implies a == 3, "the branch was taken, so it must be 3??"
assert a == 3, "the same error" when   cond
verify a == 0, "the same error" unless cond
```

The recommendation is to write as many `assert` and `assume` as possible. If
something can not happen, writing the `assume` has the advantage of allowing the
synthesis tool to generate more efficient code.

In a way, most type checks have equivalent `comptime assert` checks.

