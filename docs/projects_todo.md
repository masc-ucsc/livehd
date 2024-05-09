
### Open questions

We have a LUT node, but do we need it? We have a multiple input MUX but maybe even easier is to use the async memory.

LUT4('foo') == memory 1bit output, size 16 initialized to 'foo'

### HGLDD

 The json format used by Chisel/CIRCT to interface with tools like surfer.

 If LiveHD generates this, we can use to leverage verdi for things like Verilog and Pyrope

### Ceanup LNAST nodes

29 of the nodes are EXPR related (add, mult, ge, lt....)
15 node types are _type

What about having an "EXPR" node?

assign (or maybe new EXPR node)
  ref _dest
  type _ptr_type
  (op|ref|op)+

E.g:
 expr
   ref __t1
   op add
   ref a
   ref b
 expr
   ref __t2
   op mult
   ref __t1
   const 2
 assign
   ref dest
   type uint
   ref __t1

SAME AS: dest:uint = (a+b)*2

Then passes only need to deal with something like 20ish nodes


