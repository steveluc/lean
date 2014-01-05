Import macros.

-- In this example, we prove two simple theorems about even/odd numbers.
-- First, we define the predicates even and odd.
Definition even (a : Nat) := ∃ b, a = 2*b.
Definition odd  (a : Nat) := ∃ b, a = 2*b + 1.

-- Prove that the sum of two even numbers is even.
--
--   Notes: we use the macro
--      Obtain [bindings] ',' 'from' [expr]_1 ',' [expr]_2
--
--   It is syntax sugar for existential elimination.
--   It expands to
--
--     ExistsElim [expr]_1 (fun [binding], [expr]_2)
--
--   It is defined in the file macros.lua.
--
--   We also use the calculational proof style.
--   See doc/lean/calc.md for more information.
--
--  We use the first two Obtain-expressions to extract the
--  witnesses w1 and w2 s.t. a = 2*w1 and b = 2*w2.
--  We can do that because H1 and H2 are evidence/proof for the
--  fact that 'a' and 'b' are even.
--
--  We use a calculational proof 'calc' expression to derive
--  the witness w1+w2 for the fact that a+b is also even.
--  That is, we provide a derivation showing that a+b = 2*(w1 + w2)
Theorem EvenPlusEven {a b : Nat} (H1 : even a) (H2 : even b) : even (a + b)
:= Obtain (w1 : Nat) (Hw1 : a = 2*w1), from H1,
   Obtain (w2 : Nat) (Hw2 : b = 2*w2), from H2,
     ExistsIntro (w1 + w2)
                 (calc a + b  =  2*w1 + b      : { Hw1 }
                         ...  =  2*w1 + 2*w2   : { Hw2 }
                         ...  =  2*(w1 + w2)   : Symm (Nat::Distribute 2 w1 w2)).

-- In the following example, we omit the arguments for Nat::PlusAssoc, Refl and Nat::Distribute.
-- Lean can infer them automatically.
--
-- Refl is the reflexivity proof. (Refl a) is a proof that two
-- definitionally equal terms are indeed equal.
-- "Definitionally equal" means that they have the same normal form.
-- We can also view it as "Proof by computation".
-- The normal form of (1+1), and 2*1 is 2.
--
-- Another remark: '2*w + 1 + 1' is not definitionally equal to '2*w + 2*1'.
-- The gotcha is that '2*w + 1 + 1' is actually '(2*w + 1) + 1' since +
-- is left associative. Moreover, Lean normalizer does not use
-- any theorems such as + associativity.
Theorem OddPlusOne {a : Nat} (H : odd a) : even (a + 1)
:= Obtain (w : Nat) (Hw : a = 2*w + 1), from H,
   ExistsIntro (w + 1)
               (calc a + 1 = 2*w + 1 + 1   : { Hw }
                       ... = 2*w + (1 + 1) : Symm (Nat::PlusAssoc _ _ _)
                       ... = 2*w + 2*1     : Refl _
                       ... = 2*(w + 1)     : Symm (Nat::Distribute _ _ _)).

-- The following command displays the proof object produced by Lean after
-- expanding macros, and infering implicit/missing arguments.
print Environment 2.

-- By default, Lean does not display implicit arguments.
-- The following command will force it to display them.
SetOption pp::implicit true.

print Environment 2.

-- As an exercise, prove that the sum of two odd numbers is even,
-- and other similar theorems.