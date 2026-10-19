/-
# S3: the proved specializer with DEFERRED binder shifting

A copy of `Projection/PartialEvaluator.lean` (the PROVED specializer, S2), in
namespace `Projection.S3`, with TWO representation changes to `PVal`:

* `shifted k v`, a binder adjustment composed rather than applied
  (certio/PHASE6_PERF.md 38-39: S3 removes the shift quadratic);
* `dyns cs t`, the ARRAY SPINE (plan M6): a run of dynamic leaves held in a
  short list of arrays, so the `nthD` lookup summary indexes a spine in
  O(log n) instead of walking `n` cons cells.  The interpreter's slot
  environment is exactly such a spine -- one let-bound node value consed onto
  it per node -- and its walk was 99% of specialization time on the large
  certificates (`walkHeadAt` below).

Every place the proved specializer pattern-matches a partial value goes through
`hnf`; spines are built by `PVal.mkCons` (in `prepare`) and a variable holding
a spine reads back AS STORED; every other line is the proved code.  The
request/error types and the request bookkeeping are SHARED with `Projection`
(not copied), so `S3.mixDriver` has exactly `Projection.mixDriver`'s type.

`S3Correct.lean` proves `@Projection.mixDriver = @S3.mixDriver` -- equal on
every input, residuals, errors and fuel alike -- and registers it `@[csimp]`, so
compiled code runs this file while every theorem stays about the proved one.
-/

import LeanSemanticPrimitives.Projection.BindingTime
import LeanSemanticPrimitives.Projection.ObjectLanguageSemantics
import LeanSemanticPrimitives.Projection.PartialEvaluator

namespace Projection
namespace S3

instance : BEq SpecRequest := ⟨SpecRequest.beq⟩

/-- A run of dynamic leaves held in an array (the array spine, `PVal.dyns`):
the leaves `.dyn (arr[start] + sh)`, `.dyn (arr[start + 1] + sh)`, ..., up to
the end of `arr`.  Any three fields are a valid chunk -- `start` past the end
is the empty run -- so the representation carries no invariant: `S3Correct`'s
`rep` reads every chunk, and nothing has to be kept consistent with anything. -/
structure DChunk where
  sh    : Nat
  arr   : Array Nat
  start : Nat
  deriving Inhabited, Repr

/-- The number of leaves in a chunk. -/
def DChunk.size (c : DChunk) : Nat := c.arr.size - c.start

/-- A partial value, as in `Projection.PVal`, plus TWO representation changes.

`shifted k v` is `v` with every residual index moved out by `k`, NOT yet applied
(certio/PHASE6_PERF.md 38).  Entering `k` binders costs O(1); the offset is
applied once, at the point something inspects the value (`hnf`) or reifies it
(`toCodeAt`, `toPResAt`).  `S3Correct.lean` relates it to the proved `PVal` by
`rep (.shifted k v) = Projection.PVal.shift k (rep v)`.

`dyns cs t` is the spine whose first leaves are the chunks' dynamic leaves, in
order, followed by the spine `t`:
`rep (.dyns cs t) = cons (dyn x₀) (cons (dyn x₁) (… (rep t)))`. -/
inductive PVal where
  | stat    : Val → PVal
  | dyn     : Nat → PVal
  | cons    : PVal → PVal → PVal
  | shifted : Nat → PVal → PVal
  | dyns    : List DChunk → PVal → PVal
  deriving Inhabited, Repr

abbrev PEnv := List PVal

/-- Deferred: a leaf shifts eagerly (O(1)), a spine is wrapped, offsets compose. -/
def PVal.shift (k : Nat) (v : PVal) : PVal :=
  if k = 0 then v else
  match v with
  | .stat x       => .stat x
  | .dyn i        => .dyn (i + k)
  | .shifted k' w => .shifted (k + k') w
  | .cons a b     => .shifted k (.cons a b)
  | .dyns cs t    => .shifted k (.dyns cs t)

def PEnv.shiftBy (k : Nat) (env : PEnv) : PEnv :=
  if k = 0 then env else env.map (PVal.shift k)

/-! ## The array spine (plan M6)

The slot environment of `I_hw` is a spine built newest-first, one let-bound
node value at a time, and every slot read is an `nthD` at a static depth --
which the lookup summary answered by walking that many cons cells: O(N^2) cells
per specialization, 99% of its time on the contract-3 certificates.

A `dyns` spine holds its dynamic leaves in a list of array CHUNKS kept as a
binary counter (`insertChunk`): pushing a leaf adds a one-leaf chunk and merges
while the next chunk is no larger, so a spine of n leaves has O(log n) chunks
and each leaf is copied O(log n) times over the spine's whole life.  The
structure is persistent -- an older spine is never mutated, merging builds
fresh arrays -- which is what lets every environment that still holds an older
spine keep it.  An index is answered chunk by chunk (`chunksAt`), O(log n);
the head is popped in O(1) by advancing `start` (`popChunks`).

The offsets compose as before: a `shifted` wrapper around a `dyns` is pushed
into the chunks' `sh` only when a leaf is consed onto it (`mkCons`), which
touches each of the O(log n) chunks once. -/

/-- Add `k` to every leaf of a chunk. -/
def DChunk.addSh (k : Nat) (c : DChunk) : DChunk := { c with sh := c.sh + k }

/-- The leaves of a chunk, offset applied, as one array.  (Through a list
rather than `Array.extract`, whose `toList` lemma needs `Classical.choice`;
it runs only when chunks merge, O(n log n) leaves over a spine's life.) -/
def DChunk.vals (c : DChunk) : Array Nat := (c.arr.toList.drop c.start).toArray.map (· + c.sh)

/-- `c1`'s leaves, then `c2`'s, in one fresh array. -/
def DChunk.merge (c1 c2 : DChunk) : DChunk := ⟨0, c1.vals ++ c2.vals, 0⟩

/-- Put a chunk in front, merging while the next chunk is no larger. -/
def insertChunk (c : DChunk) : List DChunk → List DChunk
  | []         => [c]
  | c2 :: rest => if c2.size ≤ c.size then insertChunk (c.merge c2) rest else c :: c2 :: rest

/-- The first leaf of a chunk list (offset applied) and the chunks after it. -/
def popChunks : List DChunk → Option (Nat × List DChunk)
  | []      => none
  | c :: cs =>
    if c.start < c.arr.size then
      some (c.arr.getD c.start 0 + c.sh, { c with start := c.start + 1 } :: cs)
    else popChunks cs

/-- The leaf at index `n` of a chunk list (offset applied), or, past its end,
the index that is left over for the spine after it. -/
def chunksAt : List DChunk → Nat → Sum Nat Nat
  | [],      n => .inr n
  | c :: cs, n =>
    if n < c.size then .inl (c.arr.getD (c.start + n) 0 + c.sh) else chunksAt cs (n - c.size)

/-- The total offset of the `shifted` wrappers on top of a value, and what is
under them. -/
def PVal.peelShift : PVal → Nat × PVal
  | .shifted k v =>
    match PVal.peelShift v with
    | (k', w) => (k + k', w)
  | v => (0, v)

/-- `cons h t`, keeping a dynamic leaf in an array spine: onto a `dyns` spine
(under any shift) the leaf is pushed; a dynamic leaf onto anything else starts
one; any other head is a plain `cons`. -/
def PVal.mkCons (h t : PVal) : PVal :=
  match h with
  | .dyn j =>
    match PVal.peelShift t with
    | (k, .dyns cs u) =>
      .dyns (insertChunk ⟨0, #[j], 0⟩ (if k = 0 then cs else cs.map (DChunk.addSh k)))
            (PVal.shift k u)
    | _ => .dyns [⟨0, #[j], 0⟩] t
  | _ => .cons h t

/-- Head-normal form at an accumulated offset: never returns `shifted` or
`dyns`; a cons's children carry the offset, still deferred.  O(1) per wrapper
(and per empty chunk). -/
def PVal.hnfAt (acc : Nat) : PVal → PVal
  | .shifted k v => PVal.hnfAt (acc + k) v
  | .stat x      => .stat x
  | .dyn i       => .dyn (i + acc)
  | .cons a b    => .cons (PVal.shift acc a) (PVal.shift acc b)
  | .dyns cs t   =>
    match popChunks cs with
    | some (x, cs') => .cons (.dyn (x + acc)) (PVal.shift acc (.dyns cs' t))
    | none          => PVal.hnfAt acc t

def PVal.hnf (v : PVal) : PVal := PVal.hnfAt 0 v

/-- The result of specializing one term.

`cons` is the same partial structure as `PVal.cons`, but its leaves may be
arbitrary `code`: a result is not yet in the environment, so nothing has had to
shift it.

`lets` is the PACKAGE -- residual bindings carried ALONGSIDE a partial value
rather than wrapped around reified code.  Without it a partial structure dies at
every result binder: a dynamic `letIn` would return `.code (.letIn e (toCode r))`
and an unfolded call `.code (wrapLets dts (toCode r))`, both of which reify `r`
and hand the caller something opaque.  That is what would make `I_hw`'s
per-node binding self-defeating -- it would bind the node once and then flatten
the body's environment on the way out.

The binder does have a place to hang after all: a binding LIST that travels WITH
the value.  `toCode` of the package is exactly the term the old rules emitted,
which is why introducing it changes no residual and can be checked inert. -/
inductive PRes where
  | stat : Val → PRes
  | code : Term → PRes
  | cons : PRes → PRes → PRes
  | lets : List Term → PRes → PRes
  /-- A partial value CARRIED rather than converted.  `.val v` denotes exactly
  what `v.toPRes` denotes, but costs O(1) to build where `toPRes` copies the
  whole spine.  Built only through `PRes.ofPVal`, so it only ever holds a
  genuine spine.  Promoted from `Proto/PartialEvaluatorFast.lean` (CHANGE 1);
  see `certio/PHASE6_PERF.md` 26 and 27 for what it buys. -/
  | val  : PVal → PRes
  deriving Inhabited, Repr

/-- An array spine's leaves as the `consP` chain they stand for, onto `tl`. -/
def chunksCode (acc : Nat) (cs : List DChunk) (tl : Term) : Term :=
  cs.foldr (fun c r => c.vals.foldr (fun x r => .prim .consP [.var (x + acc), r]) r) tl

/-- Reify, applying accumulated offsets during the traversal it already does. -/
def PVal.toCodeAt (acc : Nat) : PVal → Term
  | .shifted k v => PVal.toCodeAt (acc + k) v
  | .stat v      => .lit v
  | .dyn k       => .var (k + acc)
  | .cons a b    => .prim .consP [PVal.toCodeAt acc a, PVal.toCodeAt acc b]
  | .dyns cs t   => chunksCode acc cs (PVal.toCodeAt acc t)

def PVal.toCode (v : PVal) : Term := PVal.toCodeAt 0 v

/-- Turn any result into residual code.  On a static value this is the `lift`
of the two-level language, and it is why lifting is always available: every
`Val` is a legal `Term.lit`.  On a partial cons it re-emits the `consP` chain --
the spine `mix` knew becomes ordinary residual code again, which is the price of
letting a partial structure escape into a dynamic context. -/
def PRes.toCode : PRes → Term
  | .stat v   => .lit v
  | .code t   => t
  | .cons a b  => .prim .consP [PRes.toCode a, PRes.toCode b]
  | .lets bs r => wrapLets bs (PRes.toCode r)
  | .val v     => PVal.toCode v

/-- An array spine's leaves as the `cons` chain of results they stand for. -/
def chunksPRes (acc : Nat) (cs : List DChunk) (tl : PRes) : PRes :=
  cs.foldr (fun c r => c.vals.foldr (fun x r => .cons (.code (.var (x + acc))) r) r) tl

def PVal.toPResAt (acc : Nat) : PVal → PRes
  | .shifted k v => PVal.toPResAt (acc + k) v
  | .stat v      => .stat v
  | .dyn k       => .code (.var (k + acc))
  | .cons a b    => .cons (PVal.toPResAt acc a) (PVal.toPResAt acc b)
  | .dyns cs t   => chunksPRes acc cs (PVal.toPResAt acc t)

def PVal.toPRes (v : PVal) : PRes := PVal.toPResAt 0 v

def PRes.ofPVal (v : PVal) : PRes :=
  match PVal.hnf v with
  | .stat x       => .stat x
  | .dyn k        => .code (.var k)
  | .cons a b     => .val (.cons a b)
  | .shifted _ _  => .val v          -- unreachable: `hnf` never returns `shifted`
  | .dyns _ _     => .val v          -- unreachable: nor `dyns`

/-- Can this result's residual code fail to produce a value?

A structural rule that DISCARDS a component -- `hd` drops the tail, `tl` drops
the head, `isNil` on a known spine drops both -- is sound only if what it drops
cannot fail, because `mixDriver_complete` reads "residual value implies source
value" and the source evaluates every operand.  `hd (consP X loop)` is the
counterexample: the source has no `Eval` at all, since `EvalList` needs every
argument to have a value, while a residual reduced to `X` does.

A `stat` is already a value and a `dyn` names a residual binding.  NOTE the
subtlety: a `dyn` is total only under the compatible-environment invariant, not
because it is syntactically a variable -- `Compat` is what says the index is in
scope, which is why this predicate is only ever used with a `Compat` in hand. -/
def PRes.total : PRes → Bool
  | .stat _        => true
  | .code (.var _) => true
  | .code _        => false
  | .cons a b      => PRes.total a && PRes.total b
  | .lets _ _      => false
  -- a partial value's leaves are `stat` or `dyn`, both total under `Compat`
  | .val _         => true

/-- What `mixTerm` returns: a partial result plus every specialization the walk
asked for. -/
abbrev MixOut := PRes × List SpecRequest

/-! ## Splitting a call's arguments

At a residual call the static operands are consumed by `mix` and the dynamic
ones survive into the residual program.  The callee's division says which is
which; a mismatch between the division and what `mixTerm` produced is an
annotation error, not a recoverable case. -/

def splitArgs : Div → List PRes → Except MixError (List Val × List Term)
  | [], [] => .ok ([], [])
  | .stat :: bs, .stat v :: rs =>
      match splitArgs bs rs with
      | .ok (vs, ts) => .ok (v :: vs, ts)
      | .error e     => .error e
  | .dyn :: bs, r :: rs =>
      match splitArgs bs rs with
      | .ok (vs, ts) => .ok (vs, r.toCode :: ts)
      | .error e     => .error e
  | .stat :: _, .code _ :: _ => .error (.notStatic "call: static parameter got residual code")
  | _, _ => .error (.badArity "call: argument count does not match the division")

/-- The environment a specialized function body is specialized under: static
parameters hold their values, and the `j`-th dynamic parameter becomes residual
index `j`. -/
def buildEnv : Div → List Val → Nat → Except MixError PEnv
  | [], [], _ => .ok []
  | .stat :: bs, v :: vs, j =>
      match buildEnv bs vs j with
      | .ok rest => .ok (.stat v :: rest)
      | .error e => .error e
  | .dyn :: bs, vs, j =>
      match buildEnv bs vs (j + 1) with
      | .ok rest => .ok (.dyn j :: rest)
      | .error e => .error e
  | _, _, _ => .error (.badArity "specialize: static argument count does not match the division")

structure Prepared where
  binds : List Term
  value : PVal
  deriving Inhabited

/-- The denotation: bindings wrapped around the value's reified code. -/
def Prepared.toPRes (p : Prepared) : PRes := .lets p.binds p.value.toPRes

/-- Split a result into the bindings its code leaves need and a partial value
whose `dyn` leaves index them.

INDEX DISCIPLINE.  `wrapLets bs body` puts `bs`'s LAST binding at index 0, so a
leaf bound as binding `j` of `k` sits at `k - 1 - j`, and a reference the caller
already held shifts UP by `k`.  Both are arithmetic on `PVal`, which is exactly
why leaves are `stat`/`dyn` and never arbitrary code.

WHY THERE IS A FALLBACK.  Two sibling subtrees that BOTH need bindings cannot
have their binding lists concatenated: the second list's terms were produced in
the caller's scope and would then sit under the first list's binders, so they
would need de Bruijn weakening -- the operation this development avoids by
threading the environment instead.  When that happens `prepare` binds the whole
subtree once and loses its spine.  `I_hw` never reaches it: the environment
argument is `cons (node value) (spine of variables)`, so exactly one side ever
needs a binding.

A `lets` PACKAGE concatenates safely, and only it does: its inner bindings
genuinely live under its outer ones, so no shift is owed. -/
def prepare : PRes → Prepared
  -- already a partial value: nothing to bind, nothing to copy
  | .val v         => ⟨[], v⟩
  | .stat v        => ⟨[], .stat v⟩
  -- already a reference; binding it again would be a wasted let
  | .code (.var i) => ⟨[], .dyn i⟩
  | .code t        => ⟨[t], .dyn 0⟩
  | .cons a b =>
    match prepare a, prepare b with
    -- `mkCons` is `cons` (`S3Correct.rep_mkCons`), kept as an array spine
    | ⟨[], va⟩, ⟨bb, vb⟩ => ⟨bb, PVal.mkCons (PVal.shift bb.length va) vb⟩
    | ⟨ba, va⟩, ⟨[], vb⟩ => ⟨ba, PVal.mkCons va (PVal.shift ba.length vb)⟩
    | _,        _        => ⟨[PRes.toCode (.cons a b)], .dyn 0⟩
  | .lets bs r =>
    let p := prepare r
    ⟨bs ++ p.binds, p.value⟩

/-! ## Structural answers for the list primitives

`hd`/`tl`/`isNil` can often be answered from a spine `mix` already holds,
without emitting code.  Three things make this delicate.

SEEING THROUGH A PACKAGE.  A result may be `lets bs (cons a b)`, and the answer
has to keep `bs`: those bindings are what preserve the failure or divergence of
work already moved out of the value, which is what keeps the completeness
direction honest.  Note the bindings themselves need NOT be total -- retaining
them is the point.

RE-WRAPPING RATHER THAN FLATTENING.  A nested package is peeled one level at a
time and re-wrapped.  Concatenating the binding lists is the obvious move and it
is WRONG: the inner package's de Bruijn references run under the outer bindings,
so `outer ++ inner` is off by the outer length -- the same index-arithmetic
class as the `wrapLets` bug this branch already hit once.

DISCARDING NEEDS A GUARD.  `hd` drops the tail, `tl` the head, `isNil` both, and
`mixDriver_complete` reads "residual value implies source value" while the
source evaluates every operand.  `hd (consP X loop)` is the counterexample.  The
guard is `PRes.total` on what is dropped.

WHAT IS DISCARDABLE IS A CONJUNCTION, and each half does different work:

    discardable  =  computation-free  AND  references-in-scope

`PRes.total` is the first half.  Once the package exists, a discarded component
holds no computation -- every node's work has already been moved into some
enclosing `lets` -- so what the spine carries is values, variables and spines of
those.  Retaining `bs` is what preserves the failure or divergence of the work
that moved.

SCOPE IS THE SECOND HALF AND DOES NOT FOLLOW FROM THE FIRST: a `code (.var i)`
leaf evaluates only if `i` is in range of the residual environment.  Nor does
the first follow from the second -- a well-scoped `.code <loop>` is perfectly
in scope and still must not be dropped, which is exactly what `total` rejects.
Both halves are needed.

`PRes.Scoped` (in `PartialEvaluatorCorrect.lean`) is the second half, and
wiring these rules in waits on the invariant that `mixTerm` preserves it.  Until
then these definitions are deliberately NOT wired into the `prim` rule. -/

/-- THE PEELS ANSWER FROM A SPINE, NEVER FROM A STATIC VALUE.

An earlier revision also had `peelHd (.stat (.cons a _)) = some (.stat a)` and
its two siblings.  They are gone, for two reasons that agree.

They were dead: a `hd`/`tl`/`isNil` whose operand is wholly static is already
answered by the STATIC branch of the `prim` case, which just runs `evalPrim`.
Reaching the static arm needs a binding-time analysis that marked a wholly
static term dynamic.  Removing them was measured INERT -- `Scaling`'s and
`Gate0`'s residuals are unchanged to the digit.

And they were not expressible in `L`.  The object specializer has to make the
same decisions as this one or the second projection's agreement is a
coincidence, and deciding "is this Val a cons" needs a primitive `L` does not
have: `isNil` separates nil from everything else, and `hd` on a non-cons is a
type error rather than a failure one can test.  Keeping an arm here that the
object cannot mirror would mean two independently designed specializers. -/
def peelHd : PRes → Option PRes
  | .lets bs r        => (peelHd r).map (PRes.lets bs)
  | .cons a b         => if b.total then some a else none
  -- a partial value's tail is total, so the guard is answered without a walk
  | .val v            => match PVal.hnf v with
                         | .cons a _ => some (PRes.ofPVal a)
                         | _         => none
  | _                 => none

def peelTl : PRes → Option PRes
  | .lets bs r        => (peelTl r).map (PRes.lets bs)
  | .cons a b         => if a.total then some b else none
  | .val v            => match PVal.hnf v with
                         | .cons _ b => some (PRes.ofPVal b)
                         | _         => none
  | _                 => none

def peelIsNil : PRes → Option PRes
  | .lets bs r        => (peelIsNil r).map (PRes.lets bs)
  | .cons a b         => if a.total && b.total then some (.stat (.bool false)) else none
  | .val v            => match PVal.hnf v with
                         | .cons _ _ => some (.stat (.bool false))
                         | _         => none
  | _                 => none

/-- `consP` discards nothing, so it may always build a spine. -/
def primStruct (p : Prim) (rs : List PRes) : Option PRes :=
  match p, rs with
  | .consP, [.stat a, .stat b] => some (.stat (.cons a b))
  | .consP, [a, b]             => some (.cons a b)
  | .hd,    [r]                => peelHd r
  | .tl,    [r]                => peelTl r
  | .isNil, [r]                => peelIsNil r
  | _, _                       => none

/-- Every result must be a value; used by the static cases, where the
congruence rule guarantees it and a violation is an annotation error. -/
def allStatic : List PRes → Except MixError (List Val)
  | []            => .ok []
  | .stat v :: rs =>
      match allStatic rs with
      | .ok vs   => .ok (v :: vs)
      | .error e => .error e
  | .code _ :: _  => .error (.notStatic "static node has a residual operand")
  | .cons _ _ :: _ => .error (.notStatic "static node has a partially static operand")
  | .lets _ _ :: _ => .error (.notStatic "static node has a residual operand")
  | .val _ :: _    => .error (.notStatic "static node has a partially static operand")

/-- The residual code of each DYNAMIC argument, in source order.  These become
the `let`s an unfold wraps around the inlined body. -/
def dynArgCodes : Div → List PRes → Except MixError (List Term)
  | [], [] => .ok []
  | .stat :: bs, _ :: rs => dynArgCodes bs rs
  | .dyn :: bs, r :: rs =>
      match dynArgCodes bs rs with
      | .ok ts   => .ok (r.toCode :: ts)
      | .error e => .error e
  | _, _ => .error (.badArity "unfold: argument count does not match the division")

/-- `[dyn 0, dyn 1, …, dyn (k-1)]` — a `caseT` alternative binds its fields with
field `j` at index `j`, matching `Eval.caseT`'s `vs ++ ρ`. -/
def freshDyns (k : Nat) : PEnv := (List.range k).map PVal.dyn

/-! ## The specializer

Fuel is a step budget, decremented on every recursive call, so this is
structural recursion on `Nat`.  It is genuinely needed: unfolding a static call
is unbounded, and a static loop that fails to terminate at specialization time
is a real failure mode of offline partial evaluation, not a Lean artifact. -/


/-! ## The `nthD` lookup summary (certio/PHASE6_PERF.md 35-37)

At a dynamic unfold of a callee whose ANNOTATED definition is the list-index
recursion, the `k + 1` unfolds a static index `k` would cost are replaced by a
walk of the known spine.  It runs on `mixPArgs`' OUTPUT -- the bindings, the
prepared list value and the static index -- so every argument is specialized
exactly once, a failed attempt costs nothing extra, and binding order, requests
and scope are `mixPArgs`' own.  Proved in `PartialEvaluatorCorrect.lean`:
`isNthD_spec`, `nthD_source`, `walkHead_ok`, `nthSummaryEnv_scoped`, and the
`ucall` cases of `mixTerm_scoped`, `mixTerm_complete` and `mixTerm_sound`. -/

/-- Walk a known spine, carrying the offset down instead of pushing it.  An
array spine is indexed chunk by chunk, not walked: O(log n) for its leaves. -/
def walkHeadAt (acc : Nat) : PVal → Nat → Option PVal
  | .shifted k v, n     => walkHeadAt (acc + k) v n
  | .cons a _,    0     => some (PVal.shift acc a)
  | .cons _ b,    n + 1 => walkHeadAt acc b n
  | .dyns cs t,   n     =>
    match chunksAt cs n with
    | .inl x => some (.dyn (x + acc))
    | .inr m => walkHeadAt acc t m
  | _,            _     => none

def walkHead (v : PVal) (n : Nat) : Option PVal := walkHeadAt 0 v n

/-- The summary, read off the environment `mixPArgs` built for a recognised
callee: the list's prepared value, then the static index.  Only a `dyn` leaf is
answered -- at lookup time in `I_hw` every leaf is one (PHASE6_PERF.md 35.1),
and a `stat` leaf is exactly where the unfold path rejects a static body, which
the summary must not turn into success.  `none` falls through to the unfold. -/
def nthSummaryEnv (f : Nat) (fd : AFunDef) (env : PEnv) : Option PRes :=
  if isNthD f fd then
    match env with
    | [pv, s] =>
      match PVal.hnf s with
      | .stat (.int k) =>
        if k < 0 then none else
          match (walkHead pv k.toNat).map PVal.hnf with
          | some (.dyn j) => some (.code (.var j))
          | _             => none
      | _ => none
    | _ => none
  else none

mutual

/-- Explicit `match` throughout rather than `do`.  `Except`'s bind does not
expose a match, so `split at h` cannot see through it -- and every case of
`mix_sound` is a `split` on exactly these branches. -/
def mixTerm : Nat → AProgram → (SpecRequest → Option Nat) → Div → PEnv → ATerm →
    Except MixError MixOut
  | 0, _, _, _, _, _ => .error .outOfFuel
  | n + 1, A, idx, Δ, env, t =>
    match t with
    | .lit v => .ok (.stat v, [])
    | .var i =>
      match Δ[i]?, env[i]? with
      | some b, some pv =>
        match b, PVal.hnf pv with
        | .stat, .stat v   => .ok (.stat v, [])
        | .dyn,  .dyn k    => .ok (.code (.var k), [])
        -- a preserved spine reads back structurally rather than being flattened,
        -- and AS STORED: peeling its head here would turn an array spine into
        -- a cons cell in front of one, and the next `mkCons` could not push
        | .dyn,  .cons _ _ => .ok (.val pv, [])
        | _,     _         => .error (.illAnnotated "var: division disagrees with the environment")
      | _,          _              => .error (.unboundVar i)
    | .lift e =>
      match mixTerm n A idx Δ env e with
      | .error z          => .error z
      | .ok (.stat v, rq) => .ok (.code (.lit v), rq)
      | .ok (.code _, _)  => .error (.notStatic "lift: operand is not static")
      | .ok (.cons _ _, _) => .error (.notStatic "lift: operand is not static")
      | .ok (.lets _ _, _) => .error (.notStatic "lift: operand is not static")
      | .ok (.val _, _)    => .error (.notStatic "lift: operand is not static")
    | .letIn _ e body =>
      match mixTerm n A idx Δ env e with
      | .error z => .error z
      | .ok (re, rq₁) =>
        match btOf Δ e, re with
        -- static binding: `mix` keeps the value and emits NO residual binder, so
        -- the residual indices already in scope do not move
        | .stat, .stat v =>
          match mixTerm n A idx (.stat :: Δ) (.stat v :: env) body with
          | .error z      => .error z
          | .ok (rb, rq₂) => .ok (rb, rq₁ ++ rq₂)
        -- dynamic binding: one residual binder is created, so everything in
        -- scope shifts by one and the bound variable becomes index 0
        | .dyn, _ =>
          match mixTerm n A idx (.dyn :: Δ) (.dyn 0 :: env.shiftBy 1) body with
          | .error z            => .error z
          | .ok (.stat _, _)    => .error (.illAnnotated "letIn: dynamic binding with a static body")
          -- the binding travels WITH the body rather than around its reified
          -- code, so a partial body keeps its spine on the way out
          | .ok (rb, rq₂)       => .ok (.lets [re.toCode] rb, rq₁ ++ rq₂)
        | .stat, .code _ => .error (.notStatic "letIn: static binding produced code")
        | .stat, .cons _ _ => .error (.notStatic "letIn: static binding produced a partial structure")
        | .stat, .lets _ _ => .error (.notStatic "letIn: static binding produced code")
        | .stat, .val _ => .error (.notStatic "letIn: static binding produced a partial structure")
    | .ite _ c a e =>
      match mixTerm n A idx Δ env c with
      | .error z => .error z
      | .ok (rc, rq₁) =>
        match btOf Δ c, rc with
        -- static condition: the untaken branch is never walked, so neither its
        -- code nor its specialization requests are emitted.  This is where a
        -- specialized interpreter loses its dispatch.
        | .stat, .stat (.bool true) =>
          match mixTerm n A idx Δ env a with
          | .error z     => .error z
          | .ok (r, rq₂) => .ok (r, rq₁ ++ rq₂)
        | .stat, .stat (.bool false) =>
          match mixTerm n A idx Δ env e with
          | .error z     => .error z
          | .ok (r, rq₂) => .ok (r, rq₁ ++ rq₂)
        | .stat, _ => .error (.notStatic "ite: static condition is not a Bool")
        | .dyn, _ =>
          match mixTerm n A idx Δ env a, mixTerm n A idx Δ env e with
          | .ok (ra, rq₂), .ok (re, rq₃) =>
              .ok (.code (.ite rc.toCode ra.toCode re.toCode), rq₁ ++ rq₂ ++ rq₃)
          | .error z, _ => .error z
          | _, .error z => .error z
    | .prim b p ts =>
      match mixTerms n A idx Δ env ts with
      | .error z => .error z
      | .ok (rs, rq) =>
        match b with
        | .stat =>
          match allStatic rs with
          | .error z => .error z
          | .ok vs =>
            match evalPrim p vs with
            | .ok v      => .ok (.stat v, rq)
            | .error msg => .error (.primFailed msg)
        | .dyn =>
          -- a guarded structural answer if the spine supports one, otherwise
          -- the opaque residual node
          match primStruct p rs with
          | some r => .ok (r, rq)
          | none   => .ok (.code (.prim p (rs.map PRes.toCode)), rq)
    | .ctorT b k ts =>
      match mixTerms n A idx Δ env ts with
      | .error z => .error z
      | .ok (rs, rq) =>
        match b with
        | .stat =>
          match allStatic rs with
          | .error z => .error z
          | .ok vs   => .ok (.stat (.ctor k vs), rq)
        | .dyn => .ok (.code (.ctorT k (rs.map PRes.toCode)), rq)
    | .caseT _ s alts =>
      match mixTerm n A idx Δ env s with
      | .error z => .error z
      | .ok (rsc, rq₁) =>
        match btOf Δ s, rsc with
        -- static scrutinee: select the alternative now and bind its fields as
        -- static values.  No residual `caseT` survives.
        | .stat, .stat (.ctor tag vs) =>
          match findAAlt alts tag with
          | none => .error (.illAnnotated "caseT: no alternative for that tag")
          | some a =>
            if a.arity = vs.length then
              match mixTerm n A idx (List.replicate a.arity .stat ++ Δ)
                              (vs.map PVal.stat ++ env) a.body with
              | .error z     => .error z
              | .ok (r, rq₂) => .ok (r, rq₁ ++ rq₂)
            else .error (.badArity "caseT: alternative arity does not match the value")
        | .stat, _ => .error (.notStatic "caseT: static scrutinee is not a constructor")
        | .dyn, _ =>
          match mixAlts n A idx Δ env alts with
          | .error z         => .error z
          | .ok (alts', rq₂) => .ok (.code (.caseT rsc.toCode alts'), rq₁ ++ rq₂)
    | .call b f ts =>
      match mixTerms n A idx Δ env ts with
      | .error z => .error z
      | .ok (rs, rq₁) =>
        match A.fn f with
        | none => .error (.unknownFun f)
        | some fd =>
          match b with
          -- a static result cannot come out of a residual call, so unfold
          | .stat =>
            if allStatDiv fd.params then
              if fd.params.length = rs.length then
                match allStatic rs with
                | .error z => .error z
                | .ok vs =>
                  match mixTerm n A idx fd.params (vs.map PVal.stat) fd.body with
                  | .error z     => .error z
                  | .ok (r, rq₂) => .ok (r, rq₁ ++ rq₂)
              else .error (.badArity "static call: argument count does not match the division")
            else .error (.illAnnotated "static call to a function with a dynamic parameter")
          -- ask the driver for a specialized copy and emit a call to it
          | .dyn =>
            match splitArgs fd.params rs with
            | .error z => .error z
            | .ok (svs, dts) =>
              match idx ⟨f, svs⟩ with
              | none   => .error (.noSpec f)
              | some k => .ok (.code (.call k dts), rq₁ ++ [⟨f, svs⟩])
    | .ucall b f ts =>
      match A.fn f with
      | none => .error (.unknownFun f)
      | some fd =>
        match b with
        | .stat =>
          match mixTerms n A idx Δ env ts with
          | .error z => .error z
          | .ok (rs, rq₁) =>
            if allStatDiv fd.params then
              if fd.params.length = rs.length then
                match allStatic rs with
                | .error z => .error z
                | .ok vs =>
                  match mixTerm n A idx fd.params (vs.map PVal.stat) fd.body with
                  | .error z     => .error z
                  | .ok (r, rq₂) => .ok (r, rq₁ ++ rq₂)
              else .error (.badArity "static unfold: argument count does not match the division")
            else .error (.illAnnotated "static unfold of a function with a dynamic parameter")
        -- inline into residual code.  Each dynamic argument is `let`-bound
        -- once, which is both what keeps `PVal.dyn` a plain index -- the body
        -- is specialized in a scope whose shape we chose -- and what stops an
        -- argument expression being duplicated at each of its uses.
        | .dyn =>
          match mixPArgs n A idx Δ env fd.params ts with
          | .error z => .error z
          | .ok (bs, env', rq₂) =>
            -- the lookup summary, on the environment just built; `none` unfolds
            match nthSummaryEnv f fd env' with
            | some r => .ok (.lets bs r, rq₂)
            | none =>
            match mixTerm n A idx fd.params env' fd.body with
            | .error z            => .error z
            | .ok (.stat _, _)    => .error (.illAnnotated "ucall: dynamic unfold with a static body")
            | .ok (rb, rq₃)       => .ok (.lets bs rb, rq₂ ++ rq₃)

def mixTerms : Nat → AProgram → (SpecRequest → Option Nat) → Div → PEnv → List ATerm →
    Except MixError (List PRes × List SpecRequest)
  | _, _, _, _, _, [] => .ok ([], [])
  | n, A, idx, Δ, env, t :: ts =>
      match mixTerm n A idx Δ env t, mixTerms n A idx Δ env ts with
      | .ok (r, rq₁), .ok (rs, rq₂) => .ok (r :: rs, rq₁ ++ rq₂)
      | .error z, _ => .error z
      | _, .error z => .error z

def mixAlts : Nat → AProgram → (SpecRequest → Option Nat) → Div → PEnv → List AAlt →
    Except MixError (List Alt × List SpecRequest)
  | _, _, _, _, _, [] => .ok ([], [])
  | n, A, idx, Δ, env, a :: as =>
      -- the alternative binds `a.arity` residual fields, so the enclosing scope
      -- shifts by that much
      match mixTerm n A idx (List.replicate a.arity .dyn ++ Δ)
                    (freshDyns a.arity ++ env.shiftBy a.arity) a.body,
            mixAlts n A idx Δ env as with
      -- one arm, not two: a static body becomes `lit v` and residual code stays
      -- as it is, which is exactly `PRes.toCode`
      | .ok (r, rq₁), .ok (as', rq₂) => .ok ((a.tag, a.arity, r.toCode) :: as', rq₁ ++ rq₂)
      | .error z, _ => .error z
      | _, .error z => .error z


/-- Argument transfer for an unfolded call, in ONE pass.

This replaced a PAIR of functions, `mixUArgs` and `inlineEnv`, that had to agree
about the binding layout and agreed only because their arithmetic matched: one
emitted a binding per dynamic parameter, the other indexed with `dynCount`.
Preparation makes that equality false -- an argument may emit zero bindings (it
was already a variable), one, or several.

A single pass returns the emitted bindings,
the callee environment, and -- implicitly, as `binds.length` -- the exact binder
depth.  The agreement is structural rather than arithmetic, and no `dynCount`
appears anywhere in it. -/
def mixPArgs : Nat → AProgram → (SpecRequest → Option Nat) → Div → PEnv → Div →
    List ATerm → Except MixError (List Term × PEnv × List SpecRequest)
  | _, _, _, _, _, [], [] => .ok ([], [], [])
  -- a static parameter is consumed by `mix` and emits no binding, so nothing
  -- after it shifts
  | n, A, idx, Δ, env, .stat :: ps, t :: ts =>
      match mixTerm n A idx Δ env t with
      | .error z          => .error z
      | .ok (.stat v, rq₁) =>
        match mixPArgs n A idx Δ env ps ts with
        | .error z              => .error z
        | .ok (bs, env', rq₂)   => .ok (bs, .stat v :: env', rq₁ ++ rq₂)
      | .ok (_, _)        => .error (.notStatic "unfold: static parameter got residual code")
  -- a dynamic parameter is PREPARED: its code leaves become bindings, its
  -- partial structure survives.  Everything after it is mixed under however
  -- many bindings it actually emitted -- zero, one, or several
  | n, A, idx, Δ, env, .dyn :: ps, t :: ts =>
      match mixTerm n A idx Δ env t with
      | .error z => .error z
      | .ok (r, rq₁) =>
        let p := prepare r
        match mixPArgs n A idx Δ (env.shiftBy p.binds.length) ps ts with
        | .error z            => .error z
        | .ok (bs, env', rq₂) =>
            -- `p.value` indexes its own bindings; the later ones sit between it
            -- and the body, so it shifts by exactly how many there are
            .ok (p.binds ++ bs, PVal.shift bs.length p.value :: env', rq₁ ++ rq₂)
  | _, _, _, _, _, _, _ =>
      .error (.badArity "unfold: argument count does not match the division")
end

/-! ## Specializing one function -/

/-- Specialize `req.funIdx` with respect to `req.staticArgs`.

The residual function keeps only the dynamic parameters (`dynCount`), and
`buildEnv` is what maps the `j`-th of them to residual index `j`.  A static
return value becomes a constant function -- correct, and the case that makes
the first projection collapse an interpreter's dispatch. -/
def mixFun (stepFuel : Nat) (A : AProgram) (idx : SpecRequest → Option Nat)
    (req : SpecRequest) : Except MixError (FunDef × List SpecRequest) :=
  match A.fn req.funIdx with
  | none    => .error (.unknownFun req.funIdx)
  | some fd =>
    match buildEnv fd.params req.staticArgs 0 with
    | .error e => .error e
    | .ok env =>
      match mixTerm stepFuel A idx fd.params env fd.body with
      | .error e     => .error e
      | .ok (r, rq)  => .ok (⟨dynCount fd.params, r.toCode⟩, rq)

/-! ## The driver

Two passes over the same pure `mixTerm`.  The first discovers which
specializations are reachable; the second generates them, now that every
request has an index.

The dummy `fun _ => some 0` in discovery is sound because an index appears only
inside emitted code, never in a control-flow decision, so the two passes explore
the same requests.  Nothing below relies on that: if discovery misses a request,
generation reports `noSpec` rather than emitting a dangling call. -/

def collectFor (stepFuel : Nat) (A : AProgram) (idx : SpecRequest → Option Nat) (f : Nat) :
    List SpecRequest → Except MixError (List SpecRequest)
  | []      => .ok []
  | r :: rs =>
    if r.funIdx = f then
      match mixFun stepFuel A idx r, collectFor stepFuel A idx f rs with
      | .ok (_, rq), .ok rest => .ok (rq ++ rest)
      | .error z,    _        => .error z
      | _,           .error z => .error z
    else collectFor stepFuel A idx f rs

def collectFrom (stepFuel : Nat) (A : AProgram) (idx : SpecRequest → Option Nat)
    (reqs : List SpecRequest) : Nat → Nat → Except MixError (List SpecRequest)
  | 0,     _ => .ok []
  | k + 1, f =>
    match collectFor stepFuel A idx f reqs, collectFrom stepFuel A idx reqs k (f + 1) with
    | .ok a,    .ok b     => .ok (a ++ b)
    | .error z, _         => .error z
    | _,        .error z  => .error z

/-- The dummy index is deliberate, and matches the object program: with the real
`indexOfReq seen` this would fail `.noSpec` on the first genuinely new request,
since by definition it is not in `seen` yet.  Discovery only reads the REQUESTS
a specialization makes; the code it emits is thrown away. -/
@[inline] def collectAll (stepFuel : Nat) (A : AProgram) (reqs : List SpecRequest) :
    Except MixError (List SpecRequest) :=
  collectFrom stepFuel A (fun _ => some 0) reqs A.funs.length 0

/-- Chaotic iteration to a fixed point.  `wlFuel` bounds the number of ROUNDS,
which is the termination condition offline partial evaluation genuinely lacks:
an infinite family of static argument tuples is a real (and useful to detect)
outcome, not a Lean limitation. -/
def closeFM (stepFuel : Nat) : Nat → AProgram → List SpecRequest →
    Except MixError (List SpecRequest)
  | 0,     _, _    => .error .outOfFuel
  | k + 1, A, seen =>
    match collectAll stepFuel A seen with
    | .error z => .error z
    | .ok fresh =>
      let s2 := groupByFun A (seen ++ addNew seen fresh)
      if seen.length = s2.length then .ok seen else closeFM stepFuel k A s2

/-- Explicit recursion rather than `mapM`: `generate_spec` has to say that
residual function `i` is the specialization of request `i`, and `List.mapM` over
`Except` does not expose a structure to induct on. -/
def generateFrom (stepFuel : Nat) (A : AProgram) (idx : SpecRequest → Option Nat) :
    List SpecRequest → Except MixError (List FunDef)
  | []      => .ok []
  | r :: rs =>
    match mixFun stepFuel A idx r, generateFrom stepFuel A idx rs with
    | .ok (fd, _), .ok fds  => .ok (fd :: fds)
    | .error e,    _        => .error e
    | _,           .error e => .error e

@[inline] def generate (stepFuel : Nat) (A : AProgram) (reqs : List SpecRequest) :
    Except MixError (List FunDef) :=
  generateFrom stepFuel A (indexOfReq reqs) reqs

/-- `mix`.  The entry request is NOT residual function 0: function-major order
puts requests for lower-numbered source functions first, so the entry's index
has to be looked up -- exactly as `MixProgram.lean` does. -/
def mixDriver (stepFuel wlFuel : Nat) (A : AProgram) (statics : List Val) :
    Except MixError Program :=
  match closeFM stepFuel wlFuel A (groupByFun A [⟨A.entry, statics⟩]) with
  | .error z => .error z
  | .ok reqs =>
    match generate stepFuel A reqs with
    | .error z  => .error z
    | .ok funs =>
      match indexOfReq reqs ⟨A.entry, statics⟩ with
      | none   => .error (.noSpec A.entry)
      | some e => .ok ⟨funs, e⟩

end S3
end Projection
