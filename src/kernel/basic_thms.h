/*
Copyright (c) 2013 Microsoft Corporation. All rights reserved.
Released under Apache 2.0 license as described in the file LICENSE.

Author: Leonardo de Moura
*/
#pragma once
#include "builtin.h"

namespace lean {

expr mk_true_ne_false();
/** \brief (Theorem) TrueNeFalse : Not(True = False) */
#define TrueNeFalse mk_true_ne_false();

expr mk_truth();
/** \brief (Theorem) Truth : True */
#define Truth mk_truth()

expr mk_em_fn();
/** \brief (Theorem) a : Bool |- EM(a) : Or(a, Not(a)) */
inline expr EM(expr const & a) { return mk_app(mk_em_fn(), a); }

expr mk_false_elim_fn();
/** \brief (Theorem) a : Bool, H : False |- FalseElim(a, H) : a */
inline expr FalseElim(expr const & a, expr const & H) { return mk_app(mk_false_elim_fn(), a, H); }

expr mk_absurd_fn();
/** \brief (Theorem) a : Bool, H1 : a, H2 : Not(a) |- Absurd(a, H1, H2) : False */
inline expr Absurd(expr const & a, expr const & H1, expr const & H2) { return mk_app(mk_absurd_fn(), a, H1, H2); }

expr mk_double_neg_fn();
/** \brief (Theorem) a : Bool |- DoubleNeg(a) : Neg(Neg(a)) = a */
inline expr DoubleNeg(expr const & a) { return mk_app(mk_double_neg_fn(), a); }

expr mk_double_neg_elim_fn();
/** \brief (Theorem) a : Bool, P : Bool -> Bool, H : P (Not (Not a)) |- DoubleNegElim(a, P, H) : P a */
inline expr DoubleNegElim(expr const & a, expr const & P, expr const & H) { return mk_app(mk_double_neg_elim_fn(), a, P, H); }

expr mk_mt_fn();
/** \brief (Theorem) a b : Bool, H1 : a => b, H2 : Not(b) |- MT(a, b, H1, H2) : Not(a) */
inline expr MT(expr const & a, expr const & b, expr const & H1, expr const & H2) { return mk_app(mk_mt_fn(), a, b, H1, H2); }

expr mk_contrapos_fn();
/** \brief (Theorem) a b : Bool, H : a => b |- Contrapos(a, b, H): Neg(b) => Neg(a) */
inline expr Contrapos(expr const & a, expr const & b, expr const & H) { return mk_app(mk_contrapos_fn(), a, b, H); }

expr mk_false_imp_any_fn();
/** \brief (Theorem) a : Bool, H : False |- a */
inline expr FalseImpAny(expr const & a, expr const & H) { return mk_app(mk_false_imp_any_fn(), a, H); }

expr mk_eq_mp_fn();
/** \brief (Theorem) a b : Bool, H1 : a = b, H2 : a |- EqMP(a, b, H1, H2) : b */
inline expr EqMP(expr const & a, expr const & b, expr const & H1, expr const & H2) { return mk_app(mk_eq_mp_fn(), a, b, H1, H2); }

expr mk_not_imp1_fn();
/** \brief (Theorem) a b : Bool, H : Not(Implies(a, b)) |- NotImp1(a, b, H) : a */
inline expr NotImp1(expr const & a, expr const & b, expr const & H) { return mk_app(mk_not_imp1_fn(), a, b, H); }

expr mk_not_imp2_fn();
/** \brief (Theorem) a b : Bool, H : Not(Implies(a, b)) |- NotImp2(a, b, H) : Not(b) */
inline expr NotImp2(expr const & a, expr const & b, expr const & H) { return mk_app(mk_not_imp2_fn(), a, b, H); }

expr mk_conj_fn();
/** \brief (Theorem) a b : Bool, H1 : a, H2 : b |- Conj(a, b, H1, H2) : And(a, b) */
inline expr Conj(expr const & a, expr const & b, expr const & H1, expr const & H2) { return mk_app(mk_conj_fn(), a, b, H1, H2); }

expr mk_conjunct1_fn();
/** \brief (Theorem) a b : Bool, H : And(a, b) |- Conjunct1(a, b, H) : a */
inline expr Conjunct1(expr const & a, expr const & b, expr const & H) { return mk_app(mk_conjunct1_fn(), a, b, H); }

expr mk_conjunct2_fn();
/** \brief (Theorem) a b : Bool, H : And(a, b) |- Conjunct2(a, b, H) : b */
inline expr Conjunct2(expr const & a, expr const & b, expr const & H) { return mk_app(mk_conjunct2_fn(), a, b, H); }

expr mk_disj1_fn();
/** \brief (Theorem) a b : Bool, H : a |- Disj1(a, b, H) : Or(a, b) */
inline expr Disj1(expr const & a, expr const & b, expr const & H) { return mk_app(mk_disj1_fn(), a, b, H); }

expr mk_disj2_fn();
/** \brief (Theorem) a b : Bool, H : b |- Disj2(a, b, H) : Or(a, b) */
inline expr Disj2(expr const & a, expr const & b, expr const & H) { return mk_app(mk_disj2_fn(), a, b, H); }

expr mk_disj_cases_fn();
/** \brief (Theorem) a b c : Bool, H1 : Or(a,b), H2 : a -> c, H3 : b -> c |- DisjCases(a, b, c, H1, H2, H3) : c */
inline expr DisjCases(expr const & a, expr const & b, expr const & c, expr const & H1, expr const & H2, expr const & H3) { return mk_app({mk_disj_cases_fn(), a, b, c, H1, H2, H3}); }

expr mk_symm_fn();
/** \brief (Theorem) A : Type u, a b : A, H : a = b |- Symm(A, a, b, H) : b = a */
inline expr Symm(expr const & A, expr const & a, expr const & b, expr const & H) { return mk_app(mk_symm_fn(), A, a, b, H); }

expr mk_trans_fn();
/** \brief (Theorem) A : Type u, a b c : A, H1 : a = b, H2 : b = c |- Trans(A, a, b, c, H1, H2) : a = c */
inline expr Trans(expr const & A, expr const & a, expr const & b, expr const & c, expr const & H1, expr const & H2) { return mk_app({mk_trans_fn(), A, a, b, c, H1, H2}); }

expr mk_trans_ext_fn();
/** \brief (Theorem) A : Type u, B : Type u, a : A, b c : B, H1 : a = b, H2 : b = c |- TransExt(A, B, a, b, c, H1, H2) : a = c */
inline expr TransExt(expr const & A, expr const & B, expr const & a, expr const & b, expr const & c, expr const & H1, expr const & H2) { return mk_app({mk_trans_ext_fn(), A, B, a, b, c, H1, H2}); }

expr mk_eqt_elim_fn();
/** \brief (Theorem) a : Bool, H : a = True |- EqTElim(a, H) : a */
inline expr EqTElim(expr const & a, expr const & H) { return mk_app(mk_eqt_elim_fn(), a, H); }

expr mk_eqt_intro_fn();
/** \brief (Theorem) a : Bool, H : a |- EqTIntro(a, H) : a = True */
inline expr EqTIntro(expr const & a, expr const & H) { return mk_app(mk_eqt_intro_fn(), a, H); }

expr mk_congr1_fn();
/** \brief (Theorem) A : Type u, B : A -> Type u, f g : (Pi x : A, B x), a : A, H : f = g |- Congr2(A, B, f, g, a, H) : f a = g a */
inline expr Congr1(expr const & A, expr const & B, expr const & f, expr const & g, expr const & a, expr const & H) { return mk_app({mk_congr1_fn(), A, B, f, g, a, H}); }

expr mk_congr2_fn();
/** \brief (Theorem) A : Type u, B : A -> Type u, f : (Pi x : A, B x), a b : A, H : a = b |- Congr1(A, B, f, a, b, H) : f a = f b */
inline expr Congr2(expr const & A, expr const & B, expr const & f, expr const & a, expr const & b, expr const & H) { return mk_app({mk_congr2_fn(), A, B, f, a, b, H}); }

expr mk_congr_fn();
/** \brief (Theorem) A : Type u, B : A -> Type u, f g : (Pi x : A, B x), a b : A, H1 : f = g, H2 : a = b |- Congr(A, B, f, g, a, b, H1, H2) : f a = g b */
inline expr Congr(expr const & A, expr const & B, expr const & f, expr const & g, expr const & a, expr const & b, expr const & H1, expr const & H2) { return mk_app({mk_congr_fn(), A, B, f, g, a, b, H1, H2}); }




expr mk_forall_elim_fn();
// \brief (Theorem) A : Type u, P : A -> Bool, H : (Forall A P), a : A |- Forallelim(A, P, H, a) : P a
inline expr ForallElim(expr const & A, expr const & P, expr const & H, expr const & a) { return mk_app(mk_forall_elim_fn(), A, P, H, a); }

/** \brief Add basic theorems to Environment */
void add_basic_thms(environment & env);

#if 0
expr mk_ext_fn();
bool is_ext_fn(expr const & e);
expr mk_foralli_fn();
bool is_foralli_fn(expr const & e);
expr mk_domain_inj_fn();
bool is_domain_inj_fn(expr const & e);
expr mk_range_inj_fn();
bool is_range_inj_fn(expr const & e);
#endif
}