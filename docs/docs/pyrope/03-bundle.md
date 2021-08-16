# Bundles

Bundles are a basic construct in Pyrope. They provide a mix of tuples and structs. Tuples are usually defined as "ordered"
sequence of elements, while structs or records are named but un-ordered data structures.

Bundles can be "ordered and named", "ordered", or just "named". A bundle can not be unnamed and unordered.

```
a.field1 = 1
a.field2 = 2    // a is named unordered
b = (f1=3,f2=4) // b is named and ordered
c = (1,d=4)     // c is ordered and unnamed (some entries are not named)
```

To access fields in a bundle we use the dot `.` or `[]`
```
a = (
  ,r1 = (b=1,c=2)
  ,r2 = (3,4)
)
// different ways to access the same field
assert a.r1.c    == 2
assert a['r1'].c == 2
assert a.r1.1    == 2
assert a.r1[1]   == 2
assert a[0][1]   == 2
assert a[0]['c'] == 2
assert a['r1.c'] == 2
assert a['r1.1'] == 2
assert a['0.c']  == 2
assert a['0.1']  == 2
assert a.0.c     == 2
assert a.0.1     == 2
assert a[':0:r1'].1    == 2 // indicate position with :num:
assert a[':0:r1.1']    == 2
assert a[':0:r1.:1:c'] == 2
```

Ordered and named fields also can use `:position:key` to indicate the position
and key. If either mismatches a compilation error is triggered.


There is introspection to check for an existing field with the `has` operator.

```
a.foo = 3
assert a has 'foo'
assert !(a has 'bar')
assert !(a has 0)     // unordered

v = (33,44,55)
assert v has 2
assert !(v has 3)
```

