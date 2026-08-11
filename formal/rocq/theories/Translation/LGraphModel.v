(** * LGraphModel — the graph-certificate language for [pass.rocq].

    Rocq port of [formal/lean/LeanSemanticPrimitives/Translation/LGraphModel.lean]
    and [formal/translation_correctness/Translation_LGraph_Model.thy].

    The generated fast model uses the width-typed [BitVec w] of
    [RocqSemanticPrimitives.SemanticPrimitives] for execution.  The *verified*
    translation path uses the small certificate language below: a graph is
    first-order DATA, and one generic evaluator interprets that data against the
    mathematical LGraph denotation.  The keystone theorem
    [evalGraphCorrectForCert] closes

        evaluated certificate  =  mathematical certificate semantics

    once and for all, so a generated file only has to instantiate it.

    ** Naming

    The certificate bit vector carries its width in the VALUE (a runtime-width
    bignum), not in the type — deliberately, so that no reasoning step ever has
    to transport along a width equality.  Its accessors are prefixed [bvc_] to
    keep them distinct from the [bv_] operations on the width-typed [BitVec w].
    The correspondence to the other two stacks is:

      Rocq [BV]/[bvc_uint]  ~  Lean [BV]/[bv_uint]  ~  Isabelle [bv]/[bv_uint]

    ** Why node ids are [N] and not [nat]

    This is the sharpest place where Rocq differs from the other two stacks.
    Lean's [Nat] is a GMP-backed binary bignum and Isabelle's [nat] is a
    code-generator abstraction, so both can carry a LiveHD node id of
    2_000_000_000 for free.  Rocq's [nat] is genuinely UNARY: the literal
    [2000000000 : nat] elaborates through [Nat.of_num_uint] and any reduction
    that forces it builds a term with two billion [S] constructors.  Certificate
    ids are compared on every evaluator step, so they must be [N] (binary),
    where a literal is O(log v) and [N.eqb] is O(log v).

    Widths stay [nat]: they index [BitVec] (a phantom parameter that is never
    reduced) and are only forced by [Z.of_nat] inside [two_pow], where they are
    bounded by the pass's [max_width] (1024 by default). *)

From Stdlib Require Import ZArith NArith List Bool Lia.
From RocqSemanticPrimitives Require Import SemanticPrimitives.

Import ListNotations.
Local Open Scope Z_scope.

(** ** LGraph operator vocabulary

    Signedness is resolved at operator-selection time (the emitter picks
    [Op_SLT] vs [Op_ULT]), not carried per node. *)

Inductive LGraphOp : Set :=
  | Op_Const (c : Z)
  | Op_Sum (n_add : nat)
  | Op_Sub
  | Op_Mult
  | Op_Div
  | Op_UDiv
  | Op_SDiv
  | Op_And
  | Op_Or
  | Op_Xor
  | Op_Ror
  | Op_Not
  | Op_LT
  | Op_GT
  | Op_ULT
  | Op_UGT
  | Op_SLT
  | Op_SGT
  | Op_EQ
  | Op_SHL
  | Op_SRA
  | Op_MuxBool
  | Op_MuxN
  | Op_Sext
  | Op_GetMask
  | Op_SetMask.

(** ** Runtime-width bit vector *)

Record BV : Set := mkBV { bv_width : nat; bv_value : Z }.

Definition mk_bv (w : nat) (v : Z) : BV :=
  {| bv_width := w; bv_value := v mod two_pow w |}.

Definition bvc_uint (x : BV) : Z := (bv_value x) mod two_pow (bv_width x).

Definition bvc_sint (x : BV) : Z :=
  let w := bv_width x in
  let u := bvc_uint x in
  if Nat.eqb w 0 then 0
  else if u <? two_pow (Nat.pred w) then u else u - two_pow w.

Definition bvc_resize (w : nat) (x : BV) : BV := mk_bv w (bvc_uint x).

(** Bridge back to the width-typed fast-model vector.  Generated
    [<Top>_outputsFromCert] uses this to read an evaluated certificate value out
    at a concrete output width. *)
Definition bv_to_bitvec (w : nat) (x : BV) : BitVec w := bv_norm w (bvc_uint x).

Definition bvc_nonzero (x : BV) : bool := negb (Z.eqb (bvc_uint x) 0).

Definition bvc_bit (x : BV) (i : nat) : bool :=
  Z.testbit (bvc_uint x) (Z.of_nat i).

Definition bits_to_int (w : nat) (f : nat -> bool) : Z :=
  fold_right (fun i acc => if f i then 2 ^ (Z.of_nat i) + acc else acc) 0 (seq 0 w).

Definition bvc_bitwise (w : nat) (f : bool -> bool -> bool) (a b : BV) : BV :=
  mk_bv w (bits_to_int w (fun i => f (bvc_bit a i) (bvc_bit b i))).

Definition bvc_not (w : nat) (a : BV) : BV :=
  mk_bv w (bits_to_int w (fun i => negb (bvc_bit a i))).

(** Arithmetic shift right.  [Z.div] FLOORS, which is the sign-extending
    behaviour hardware implements, and it is the same rounding the fast-model
    [sem_sra] uses — so the two views agree on negative operands by
    construction.  The shift amount is clamped before exponentiating (see
    [clamp_shift]); the clamp is semantically the identity because shifting a
    [w]-bit value right by [w] or more already saturates to all sign bits. *)
Definition bvc_sra (w : nat) (x shamt : BV) : BV :=
  mk_bv w (bvc_sint x / 2 ^ (clamp_shift w (bvc_uint shamt))).

Definition bvc_sdiv (w : nat) (a b : BV) : BV :=
  mk_bv w (if Z.eqb (bvc_uint b) 0 then 0
           else trunc_div_int (bvc_sint a) (bvc_sint b)).

Definition mask_indices_bv (m : BV) : list nat :=
  filter (fun i => bvc_bit m i) (seq 0 (bv_width m)).

Fixpoint pack_low_bv (x : BV) (is : list nat) : Z :=
  match is with
  | [] => 0
  | i :: is' =>
      let packed := pack_low_bv x is' in
      if bvc_bit x i then 2 ^ (Z.of_nat (length is')) + packed else packed
  end.

Definition bvc_get_mask (w : nat) (x m : BV) : BV :=
  mk_bv w (pack_low_bv x (rev (mask_indices_bv m))).

Definition bvc_set_bit (x : BV) (i : nat) (b : bool) : BV :=
  let w := bv_width x in
  let u := bvc_uint x in
  mk_bv w
    (if b then u + (if bvc_bit x i then 0 else 2 ^ (Z.of_nat i))
     else u - (if bvc_bit x i then 2 ^ (Z.of_nat i) else 0)).

Definition bvc_set_mask (w : nat) (a m v : BV) : BV :=
  let idxs := mask_indices_bv m in
  bvc_resize w
    (fold_left (fun acc p => bvc_set_bit acc (snd p) (bvc_bit v (fst p)))
               (combine (seq 0 (length idxs)) idxs)
               a).

(** ** Denotation and evaluation

    [denote_op] is the mathematical denotation; [eval_op] is the executable
    evaluator.  The bodies are IDENTICAL, and they are kept as two definitions
    on purpose so generated lemmas can name each side of the bridge.
    [eval_op_correct] below closes the gap by conversion.

    Every arm is total: a shape that does not match the expected arity falls
    through to [mk_bv w 0] rather than getting stuck, because a stuck reduction
    in Rocq builds an enormous partially-evaluated term. *)

Definition Zsum (l : list Z) : Z := fold_right Z.add 0 l.
Definition Zprod (l : list Z) : Z := fold_right Z.mul 1 l.

Definition denote_op (o : LGraphOp) (w : nat) (args : list BV) : BV :=
  match o with
  | Op_Const c => mk_bv w c
  | Op_Sum n_add =>
      let us := map bvc_uint args in
      mk_bv w (Zsum (firstn n_add us) - Zsum (skipn n_add us))
  | Op_Sub =>
      match args with
      | [a; b] => mk_bv w (bvc_uint a - bvc_uint b)
      | _ => mk_bv w 0
      end
  | Op_Mult => mk_bv w (Zprod (map bvc_uint args))
  | Op_Div =>
      match args with
      | [a; b] => mk_bv w (if Z.eqb (bvc_uint b) 0 then 0
                           else bvc_uint a / bvc_uint b)
      | _ => mk_bv w 0
      end
  | Op_UDiv =>
      match args with
      | [a; b] => mk_bv w (if Z.eqb (bvc_uint b) 0 then 0
                           else bvc_uint a / bvc_uint b)
      | _ => mk_bv w 0
      end
  | Op_SDiv =>
      match args with
      | [a; b] => bvc_sdiv w a b
      | _ => mk_bv w 0
      end
  | Op_And =>
      match args with
      | [] => mk_bv w 0
      | a :: rest =>
          fold_left (fun acc b => bvc_bitwise w andb acc b) rest (bvc_resize w a)
      end
  | Op_Or => fold_left (fun acc b => bvc_bitwise w orb acc b) args (mk_bv w 0)
  | Op_Xor => fold_left (fun acc b => bvc_bitwise w xorb acc b) args (mk_bv w 0)
  | Op_Ror => mk_bv w (if existsb bvc_nonzero args then 1 else 0)
  | Op_Not =>
      match args with
      | [a] => bvc_not w a
      | _ => mk_bv w 0
      end
  | Op_LT =>
      match args with
      | [a; b] => mk_bv w (if bvc_uint a <? bvc_uint b then 1 else 0)
      | _ => mk_bv w 0
      end
  | Op_GT =>
      match args with
      | [a; b] => mk_bv w (if bvc_uint b <? bvc_uint a then 1 else 0)
      | _ => mk_bv w 0
      end
  | Op_ULT =>
      match args with
      | [a; b] => mk_bv w (if bvc_uint a <? bvc_uint b then 1 else 0)
      | _ => mk_bv w 0
      end
  | Op_UGT =>
      match args with
      | [a; b] => mk_bv w (if bvc_uint b <? bvc_uint a then 1 else 0)
      | _ => mk_bv w 0
      end
  | Op_SLT =>
      match args with
      | [a; b] => mk_bv w (if bvc_sint a <? bvc_sint b then 1 else 0)
      | _ => mk_bv w 0
      end
  | Op_SGT =>
      match args with
      | [a; b] => mk_bv w (if bvc_sint b <? bvc_sint a then 1 else 0)
      | _ => mk_bv w 0
      end
  | Op_EQ =>
      match args with
      | [] => mk_bv w 1
      | a :: rest =>
          mk_bv w (if forallb (fun b => Z.eqb (bvc_uint b) (bvc_uint a)) rest
                   then 1 else 0)
      end
  | Op_SHL =>
      match args with
      | [] => mk_bv w 0
      | a :: bs =>
          fold_left
            (fun acc b =>
               bvc_bitwise w xorb acc
                 (mk_bv w (bvc_uint a * 2 ^ (clamp_shift w (bvc_uint b)))))
            bs (mk_bv w 0)
      end
  | Op_SRA =>
      match args with
      | [a; b] => bvc_sra w a b
      | _ => mk_bv w 0
      end
  | Op_MuxBool =>
      match args with
      | [sel; false_v; true_v] =>
          if bvc_nonzero sel then bvc_resize w true_v else bvc_resize w false_v
      | _ => mk_bv w 0
      end
  | Op_MuxN =>
      match args with
      | [] => mk_bv w 0
      | sel :: rest =>
          (* Guard the index in Z BEFORE Z.to_nat: a 32-bit selector can hold a
             value near 2^32, and Z.to_nat of that would build a unary nat of
             that size.  The bound check is O(log v); only an in-range index is
             ever converted. *)
          let iz := bvc_uint sel in
          if iz <? Z.of_nat (length rest) then
            match nth_error rest (Z.to_nat iz) with
            | Some v => bvc_resize w v
            | None => mk_bv w 0
            end
          else mk_bv w 0
      end
  | Op_Sext =>
      match args with
      | [a; amount] =>
          (* Kept entirely in Z for the same reason as Op_MuxN.  The [w <= n]
             arm is not an approximation: when the sign position is at or above
             the output width, both branches of the general formula agree with
             [mk_bv w (bvc_uint a)] modulo 2^w. *)
          let nz := bvc_uint amount in
          if Z.eqb nz 0 then mk_bv w 0
          else if Z.of_nat w <=? nz then mk_bv w (bvc_uint a)
          else
            let u := (bvc_uint a) mod (2 ^ nz) in
            if u <? 2 ^ (nz - 1) then mk_bv w u else mk_bv w (u - 2 ^ nz)
      | _ => mk_bv w 0
      end
  | Op_GetMask =>
      match args with
      | [a; m] => bvc_get_mask w a m
      | _ => mk_bv w 0
      end
  | Op_SetMask =>
      match args with
      | [a; m; v] => bvc_set_mask w a m v
      | _ => mk_bv w 0
      end
  end.

Definition eval_op (o : LGraphOp) (w : nat) (args : list BV) : BV :=
  match o with
  | Op_Const c => mk_bv w c
  | Op_Sum n_add =>
      let us := map bvc_uint args in
      mk_bv w (Zsum (firstn n_add us) - Zsum (skipn n_add us))
  | Op_Sub =>
      match args with
      | [a; b] => mk_bv w (bvc_uint a - bvc_uint b)
      | _ => mk_bv w 0
      end
  | Op_Mult => mk_bv w (Zprod (map bvc_uint args))
  | Op_Div =>
      match args with
      | [a; b] => mk_bv w (if Z.eqb (bvc_uint b) 0 then 0
                           else bvc_uint a / bvc_uint b)
      | _ => mk_bv w 0
      end
  | Op_UDiv =>
      match args with
      | [a; b] => mk_bv w (if Z.eqb (bvc_uint b) 0 then 0
                           else bvc_uint a / bvc_uint b)
      | _ => mk_bv w 0
      end
  | Op_SDiv =>
      match args with
      | [a; b] => bvc_sdiv w a b
      | _ => mk_bv w 0
      end
  | Op_And =>
      match args with
      | [] => mk_bv w 0
      | a :: rest =>
          fold_left (fun acc b => bvc_bitwise w andb acc b) rest (bvc_resize w a)
      end
  | Op_Or => fold_left (fun acc b => bvc_bitwise w orb acc b) args (mk_bv w 0)
  | Op_Xor => fold_left (fun acc b => bvc_bitwise w xorb acc b) args (mk_bv w 0)
  | Op_Ror => mk_bv w (if existsb bvc_nonzero args then 1 else 0)
  | Op_Not =>
      match args with
      | [a] => bvc_not w a
      | _ => mk_bv w 0
      end
  | Op_LT =>
      match args with
      | [a; b] => mk_bv w (if bvc_uint a <? bvc_uint b then 1 else 0)
      | _ => mk_bv w 0
      end
  | Op_GT =>
      match args with
      | [a; b] => mk_bv w (if bvc_uint b <? bvc_uint a then 1 else 0)
      | _ => mk_bv w 0
      end
  | Op_ULT =>
      match args with
      | [a; b] => mk_bv w (if bvc_uint a <? bvc_uint b then 1 else 0)
      | _ => mk_bv w 0
      end
  | Op_UGT =>
      match args with
      | [a; b] => mk_bv w (if bvc_uint b <? bvc_uint a then 1 else 0)
      | _ => mk_bv w 0
      end
  | Op_SLT =>
      match args with
      | [a; b] => mk_bv w (if bvc_sint a <? bvc_sint b then 1 else 0)
      | _ => mk_bv w 0
      end
  | Op_SGT =>
      match args with
      | [a; b] => mk_bv w (if bvc_sint b <? bvc_sint a then 1 else 0)
      | _ => mk_bv w 0
      end
  | Op_EQ =>
      match args with
      | [] => mk_bv w 1
      | a :: rest =>
          mk_bv w (if forallb (fun b => Z.eqb (bvc_uint b) (bvc_uint a)) rest
                   then 1 else 0)
      end
  | Op_SHL =>
      match args with
      | [] => mk_bv w 0
      | a :: bs =>
          fold_left
            (fun acc b =>
               bvc_bitwise w xorb acc
                 (mk_bv w (bvc_uint a * 2 ^ (clamp_shift w (bvc_uint b)))))
            bs (mk_bv w 0)
      end
  | Op_SRA =>
      match args with
      | [a; b] => bvc_sra w a b
      | _ => mk_bv w 0
      end
  | Op_MuxBool =>
      match args with
      | [sel; false_v; true_v] =>
          if bvc_nonzero sel then bvc_resize w true_v else bvc_resize w false_v
      | _ => mk_bv w 0
      end
  | Op_MuxN =>
      match args with
      | [] => mk_bv w 0
      | sel :: rest =>
          (* Guard the index in Z BEFORE Z.to_nat: a 32-bit selector can hold a
             value near 2^32, and Z.to_nat of that would build a unary nat of
             that size.  The bound check is O(log v); only an in-range index is
             ever converted. *)
          let iz := bvc_uint sel in
          if iz <? Z.of_nat (length rest) then
            match nth_error rest (Z.to_nat iz) with
            | Some v => bvc_resize w v
            | None => mk_bv w 0
            end
          else mk_bv w 0
      end
  | Op_Sext =>
      match args with
      | [a; amount] =>
          (* Kept entirely in Z for the same reason as Op_MuxN.  The [w <= n]
             arm is not an approximation: when the sign position is at or above
             the output width, both branches of the general formula agree with
             [mk_bv w (bvc_uint a)] modulo 2^w. *)
          let nz := bvc_uint amount in
          if Z.eqb nz 0 then mk_bv w 0
          else if Z.of_nat w <=? nz then mk_bv w (bvc_uint a)
          else
            let u := (bvc_uint a) mod (2 ^ nz) in
            if u <? 2 ^ (nz - 1) then mk_bv w u else mk_bv w (u - 2 ^ nz)
      | _ => mk_bv w 0
      end
  | Op_GetMask =>
      match args with
      | [a; m] => bvc_get_mask w a m
      | _ => mk_bv w 0
      end
  | Op_SetMask =>
      match args with
      | [a; m; v] => bvc_set_mask w a m v
      | _ => mk_bv w 0
      end
  end.

(** ** Certificate records *)

Record NodeCert : Set := mkNodeCert {
  nc_nid   : N;
  nc_op    : LGraphOp;
  nc_width : nat;
  nc_deps  : list N
}.

Record GraphCert : Type := mkGraphCert {
  gc_topo    : list N;
  gc_sources : list N;
  gc_nodes   : N -> option NodeCert
}.

Definition nodes_of_list (cs : list NodeCert) (n : N) : option NodeCert :=
  find (fun c => N.eqb (nc_nid c) n) cs.

Definition depopts_of (G : GraphCert) (n : N) : list N :=
  match gc_nodes G n with
  | None => []
  | Some c => nc_deps c
  end.

Definition node_width_of (G : GraphCert) (n : N) : option nat :=
  match gc_nodes G n with
  | None => None
  | Some c => Some (nc_width c)
  end.

Definition node_op_of (G : GraphCert) (n : N) : option LGraphOp :=
  match gc_nodes G n with
  | None => None
  | Some c => Some (nc_op c)
  end.

(** ** Well-formedness

    A [Prop] version for reasoning and boolean versions for the [cert_wf] proof
    modes of the pass, which discharge them by computation. *)

Definition list_mem (x : N) (l : list N) : bool := existsb (N.eqb x) l.

Fixpoint list_nodup_b (l : list N) : bool :=
  match l with
  | [] => true
  | x :: xs => negb (list_mem x xs) && list_nodup_b xs
  end.

Definition graphCertWf (G : GraphCert) : Prop :=
  NoDup (gc_topo G) /\
  NoDup (gc_sources G) /\
  (forall n, In n (gc_topo G) -> ~ In n (gc_sources G)) /\
  (forall n, In n (gc_topo G) ->
     match gc_nodes G n with
     | None => False
     | Some c =>
         nc_nid c = n /\
         (0 < nc_width c)%nat /\
         (forall d, In d (nc_deps c) ->
            In d (gc_topo G) \/ In d (gc_sources G))
     end) /\
  (forall n, In n (gc_sources G) -> gc_nodes G n = None).

Fixpoint depsBefore (seen : list N) (cs : list NodeCert) : bool :=
  match cs with
  | [] => true
  | c :: cs' =>
      forallb (fun d => list_mem d seen) (nc_deps c)
      && depsBefore (seen ++ [nc_nid c]) cs'
  end.

Definition graphCertWfBool (cs : list NodeCert) (srcs : list N) : bool :=
  list_nodup_b (map nc_nid cs)
  && list_nodup_b srcs
  && forallb (fun n => negb (list_mem n srcs)) (map nc_nid cs)
  && forallb (fun c =>
       Nat.ltb 0 (nc_width c)
       && forallb (fun d => list_mem d (map nc_nid cs) || list_mem d srcs)
                  (nc_deps c)) cs
  && depsBefore srcs cs.

Definition nodeCertChunkWfBool (allIds srcs : list N) (cs : list NodeCert) : bool :=
  list_nodup_b (map nc_nid cs)
  && forallb (fun n => list_mem n allIds) (map nc_nid cs)
  && forallb (fun n => negb (list_mem n srcs)) (map nc_nid cs)
  && forallb (fun c =>
       Nat.ltb 0 (nc_width c)
       && forallb (fun d => list_mem d allIds || list_mem d srcs) (nc_deps c)) cs.

Definition constNodeCertWfBool (c : NodeCert) : bool :=
  match nc_op c with
  | Op_Const _ => Nat.ltb 0 (nc_width c) && match nc_deps c with [] => true | _ => false end
  | _ => false
  end.

Definition validDepsBool (validRef : N -> bool) (ds : list N) : bool :=
  forallb validRef ds.

Definition nodeCertDeps (cs : list NodeCert) : list N :=
  concat (map nc_deps cs).

Definition simpleOpCertWfBool (o : LGraphOp) (w : nat) (ds : list N) : bool :=
  match o with
  | Op_Const _ => match ds with [] => true | _ => false end
  | Op_Sum nAdd => Nat.ltb 0 (length ds) && Nat.leb nAdd (length ds)
  | Op_And => Nat.ltb 0 (length ds)
  | Op_Or => Nat.ltb 0 (length ds)
  | Op_Xor => Nat.ltb 0 (length ds)
  | Op_Ror => Nat.ltb 0 (length ds) && Nat.eqb w 1
  | Op_Not => Nat.eqb (length ds) 1
  | Op_EQ => Nat.eqb (length ds) 2 && Nat.eqb w 1
  | Op_ULT => Nat.eqb (length ds) 2 && Nat.eqb w 1
  | Op_UGT => Nat.eqb (length ds) 2 && Nat.eqb w 1
  | Op_SLT => Nat.eqb (length ds) 2 && Nat.eqb w 1
  | Op_SGT => Nat.eqb (length ds) 2 && Nat.eqb w 1
  | Op_GetMask => Nat.eqb (length ds) 2
  | Op_MuxBool => Nat.eqb (length ds) 3
  | Op_MuxN => Nat.ltb 1 (length ds)
  | Op_SHL => Nat.eqb (length ds) 2
  | Op_SRA => Nat.eqb (length ds) 2
  | Op_Sext => Nat.eqb (length ds) 2
  | _ => false
  end.

Definition simpleNodeCertShapeWfBool (c : NodeCert) : bool :=
  Nat.ltb 0 (nc_width c) && simpleOpCertWfBool (nc_op c) (nc_width c) (nc_deps c).

Definition simpleNodeCertWfBool (validRef : N -> bool) (c : NodeCert) : bool :=
  Nat.ltb 0 (nc_width c)
  && validDepsBool validRef (nc_deps c)
  && simpleOpCertWfBool (nc_op c) (nc_width c) (nc_deps c).

(** ** The generic evaluator and denotation *)

Definition envSet (rho : N -> BV) (n : N) (v : BV) : N -> BV :=
  fun m => if N.eqb m n then v else rho m.

Definition denoteNode (G : GraphCert) (sourceEnv : N -> BV) (n : N) : BV :=
  match gc_nodes G n with
  | None => sourceEnv n
  | Some c => denote_op (nc_op c) (nc_width c) (map sourceEnv (nc_deps c))
  end.

Definition evalNode (G : GraphCert) (rho : N -> BV) (n : N) : BV :=
  match gc_nodes G n with
  | None => rho n
  | Some c => eval_op (nc_op c) (nc_width c) (map rho (nc_deps c))
  end.

Definition denoteNodeEnv (G : GraphCert) (rho : N -> BV) (n : N) : BV :=
  match gc_nodes G n with
  | None => rho n
  | Some c => denote_op (nc_op c) (nc_width c) (map rho (nc_deps c))
  end.

Fixpoint evalGraph (order : list N) (G : GraphCert) (rho : N -> BV)
  : N -> BV :=
  match order with
  | [] => rho
  | n :: ns => evalGraph ns G (envSet rho n (evalNode G rho n))
  end.

Fixpoint denoteGraph (order : list N) (G : GraphCert) (rho : N -> BV)
  : N -> BV :=
  match order with
  | [] => rho
  | n :: ns => denoteGraph ns G (envSet rho n (denoteNodeEnv G rho n))
  end.

Definition graphDenotation (order : list N) (G : GraphCert)
                           (sourceEnv : N -> BV) : N -> BV :=
  denoteGraph order G sourceEnv.

Definition envCorrectOn (ns : list N) (rho denote : N -> BV) : Prop :=
  forall n, In n ns -> rho n = denote n.

(** ** The keystone

    [eval_op] and [denote_op] have identical bodies, so the first step is pure
    conversion.  Everything above it is a structural induction.  Generated
    per-design files instantiate [evalGraphCorrectForCert] and never re-prove
    any of this. *)

Theorem eval_op_correct : forall (o : LGraphOp) (w : nat) (args : list BV),
  eval_op o w args = denote_op o w args.
Proof. reflexivity. Qed.

Theorem evalNode_eq_denoteNodeEnv : forall (G : GraphCert) (rho : N -> BV) (n : N),
  evalNode G rho n = denoteNodeEnv G rho n.
Proof.
  intros G rho n. unfold evalNode, denoteNodeEnv.
  destruct (gc_nodes G n) as [c |]; [apply eval_op_correct | reflexivity].
Qed.

Theorem evalGraph_eq_denoteGraph : forall (order : list N) (G : GraphCert) (rho : N -> BV),
  evalGraph order G rho = denoteGraph order G rho.
Proof.
  intros order. induction order as [| n ns IH]; intros G rho; simpl.
  - reflexivity.
  - rewrite evalNode_eq_denoteNodeEnv. apply IH.
Qed.

Theorem evalGraphCorrect : forall (order : list N) (G : GraphCert) (sourceEnv : N -> BV),
  envCorrectOn order
    (evalGraph order G sourceEnv)
    (graphDenotation order G sourceEnv).
Proof.
  intros order G sourceEnv. unfold envCorrectOn, graphDenotation. intros n _.
  rewrite evalGraph_eq_denoteGraph. reflexivity.
Qed.

Theorem evalGraphCorrectForCert : forall (G : GraphCert) (sourceEnv : N -> BV),
  envCorrectOn (gc_topo G)
    (evalGraph (gc_topo G) G sourceEnv)
    (graphDenotation (gc_topo G) G sourceEnv).
Proof.
  intros G sourceEnv. apply evalGraphCorrect.
Qed.
