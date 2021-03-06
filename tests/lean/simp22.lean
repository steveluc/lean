import   cast
variable vec    : Nat → Type
variable concat {n m : Nat} (v : vec n) (w : vec m) : vec (n + m)
infixl   65 ; : concat
axiom    concat_assoc {n1 n2 n3 : Nat} (v1 : vec n1) (v2 : vec n2) (v3 : vec n3) :
             (v1 ; v2) ; v3 = cast (congr2 vec (symm (Nat::add_assoc n1 n2 n3)))
                              (v1 ; (v2 ; v3))
variable empty : vec 0
axiom    concat_empty {n : Nat} (v : vec n) :
             v ; empty = cast (congr2 vec (symm (Nat::add_zeror n)))
                         v

rewrite_set simple
-- The simplification rules used for Nat and Vectors should "mirror" each other.
-- Concatenation is not commutative. So, by adding Nat::add_comm to the
-- rewrite set, we prevent the simplifier from reducing the following example
add_rewrite concat_assoc concat_empty Nat::add_assoc Nat::add_zeror Nat::add_comm : simple

variable n : Nat
variable v : vec n
variable w : vec n
variable f {A : TypeM} : A → A

(*
local t = parse_lean([[ f ((v ; w) ; empty ; (v ; empty)) ]])
print(t)
print("===>")
local t2, pr = simplify(t, "simple")
print(t2)
print(pr)
get_environment():type_check(pr)
*)

-- Now, if we disable Nat::add_comm, the simplifier works
disable_rewrite Nat::add_comm : simple
print "After disabling Nat::add_comm"

(*
local t = parse_lean([[ f ((v ; w) ; empty ; (v ; empty)) ]])
print(t)
print("===>")
local t2, pr = simplify(t, "simple")
print(t2)
print(pr)
get_environment():type_check(pr)
*)
