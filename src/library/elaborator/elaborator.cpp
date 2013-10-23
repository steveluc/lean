/*
Copyright (c) 2013 Microsoft Corporation. All rights reserved.
Released under Apache 2.0 license as described in the file LICENSE.

Author: Leonardo de Moura
*/
#include <memory>
#include <vector>
#include <utility>
#include "util/pdeque.h"
#include "kernel/formatter.h"
#include "kernel/free_vars.h"
#include "kernel/normalizer.h"
#include "kernel/instantiate.h"
#include "kernel/replace.h"
#include "kernel/builtin.h"
#include "library/type_inferer.h"
#include "library/update_expr.h"
#include "library/reduce.h"
#include "library/elaborator/elaborator.h"
#include "library/elaborator/elaborator_trace.h"

namespace lean {
static name g_x_name("x");

class elaborator::imp {
    typedef pdeque<unification_constraint> cnstr_queue;

    struct state {
        metavar_env m_menv;
        cnstr_queue m_queue;

        state(metavar_env const & menv, unsigned num_cnstrs, unification_constraint const * cnstrs):
            m_menv(menv) {
            for (unsigned i = 0; i < num_cnstrs; i++)
                m_queue.push_back(cnstrs[i]);
        }

        state(metavar_env const & menv, cnstr_queue const & q):
            m_menv(menv),
            m_queue(q) {
        }
    };

    /**
       \brief Base class for case splits performed by the elaborator.
    */
    struct case_split {
        trace              m_curr_assumption; // trace object used to justify current split
        state              m_prev_state;
        std::vector<trace> m_failed_traces;   // traces/justifications for failed branches

        case_split(state const & prev_state):m_prev_state(prev_state) {}
        virtual ~case_split() {}

        virtual bool next(imp & owner) = 0;
    };

    /**
       \brief Case-split object for choice constraints.
    */
    struct choice_case_split : public case_split {
        unsigned               m_idx;
        unification_constraint m_choice;

        choice_case_split(unification_constraint const & c, state const & prev_state):
            case_split(prev_state),
            m_idx(0),
            m_choice(c) {
        }

        virtual ~choice_case_split() {}

        virtual bool next(imp & owner) {
            return owner.next_choice_case(*this);
        }
    };

    /**
       \brief General purpose case split object
    */
    struct generic_case_split : public case_split {
        unification_constraint m_constraint;
        unsigned               m_idx;    // current alternative
        std::vector<state>     m_states; // alternatives
        std::vector<trace>     m_assumptions; // assumption for each alternative

        generic_case_split(unification_constraint const & cnstr, state const & prev_state):
            case_split(prev_state),
            m_constraint(cnstr),
            m_idx(0) {
        }

        virtual ~generic_case_split() {}

        virtual bool next(imp & owner) {
            return owner.next_generic_case(*this);
        }

        void push_back(state const & s, trace const & tr) {
            m_states.push_back(s);
            m_assumptions.push_back(tr);
        }
    };

    struct synthesizer_case_split : public case_split {
        expr                                 m_metavar;
        std::unique_ptr<synthesizer::result> m_alternatives;

        synthesizer_case_split(expr const & m, std::unique_ptr<synthesizer::result> & r, state const & prev_state):
            case_split(prev_state),
            m_metavar(m),
            m_alternatives(std::move(r)) {
        }

        virtual ~synthesizer_case_split() {}
    };

    struct plugin_case_split : public case_split {
        unification_constraint                     m_constraint;
        std::unique_ptr<elaborator_plugin::result> m_alternatives;

        plugin_case_split(unification_constraint const & cnstr, std::unique_ptr<elaborator_plugin::result> & r, state const & prev_state):
            case_split(prev_state),
            m_constraint(cnstr),
            m_alternatives(std::move(r)) {
        }

        virtual ~plugin_case_split() {}

        virtual bool next(imp & owner) {
            return owner.next_plugin_case(*this);
        }
    };

    environment const &                      m_env;
    type_inferer                             m_type_inferer;
    normalizer                               m_normalizer;
    state                                    m_state;
    std::vector<std::unique_ptr<case_split>> m_case_splits;
    std::shared_ptr<synthesizer>             m_synthesizer;
    std::shared_ptr<elaborator_plugin>       m_plugin;
    unsigned                                 m_next_id;
    int                                      m_quota;
    trace                                    m_conflict;
    bool                                     m_first;
    bool                                     m_interrupted;


    // options
    bool                                     m_use_traces;
    bool                                     m_use_normalizer;

    void set_options(options const &) {
        m_use_traces     = true;
        m_use_normalizer = true;
    }

    void reset_quota() {
        m_quota = m_state.m_queue.size();
    }

    trace mk_assumption() {
        unsigned id = m_next_id;
        m_next_id++;
        return trace(new assumption_trace(id));
    }

    /** \brief Add given constraint to the front of the current constraint queue */
    void push_front(unification_constraint const & c) {
        reset_quota();
        m_state.m_queue.push_front(c);
    }

    /** \brief Add given constraint to the end of the current constraint queue */
    void push_back(unification_constraint const & c) {
        m_state.m_queue.push_back(c);
    }

    /** \brief Return true iff \c m is an assigned metavariable in the current state */
    bool is_assigned(expr const & m) const {
        lean_assert(is_metavar(m));
        return m_state.m_menv.is_assigned(m);
    }

    /** \brief Return the substitution for an assigned metavariable */
    expr get_mvar_subst(expr const & m) const {
        lean_assert(is_metavar(m));
        lean_assert(is_assigned(m));
        return m_state.m_menv.get_subst(m);
    }

    /** \brief Return the trace/justification for an assigned metavariable */
    trace get_mvar_trace(expr const & m) const {
        lean_assert(is_metavar(m));
        lean_assert(is_assigned(m));
        return m_state.m_menv.get_trace(m);
    }

    /** \brief Return the type of an metavariable */
    expr get_mvar_type(expr const & m) {
        lean_assert(is_metavar(m));
        return m_state.m_menv.get_type(m);
    }

    /**
       \brief Return true iff \c e contains the metavariable \c m.
       The substitutions in the current state are taken into account.
    */
    bool has_metavar(expr const & e, expr const & m) const {
        return ::lean::has_metavar(e, m, m_state.m_menv.get_substitutions());
    }

    static bool has_metavar(expr const & e) {
        return ::lean::has_metavar(e);
    }

    /**
       \brief Return true iff \c e contains an assigned metavariable in
       the current state.
    */
    bool has_assigned_metavar(expr const & e) const {
        return ::lean::has_assigned_metavar(e, m_state.m_menv.get_substitutions());
    }

    /**
        \brief Return a unassigned metavariable in the current state.
        Return the anonymous name if the state does not contain unassigned metavariables.
    */
    name find_unassigned_metavar() const {
        return m_state.m_menv.find_unassigned_metavar();
    }

    /** \brief Return true if \c a is of the form <tt>(?m ...)</tt> */
    static bool is_meta_app(expr const & a) {
        return is_app(a) && is_metavar(arg(a, 0));
    }

    /** \brief Return true iff \c a is a metavariable or if \c a is an application where the function is a metavariable */
    static bool is_meta(expr const & a) {
        return is_metavar(a) || is_meta_app(a);
    }

    /** \brief Return true iff \c a is a metavariable and has a meta context. */
    static bool is_metavar_with_context(expr const & a) {
        return is_metavar(a) && has_local_context(a);
    }

    /** \brief Return true if \c a is of the form <tt>(?m[...] ...)</tt> */
    static bool is_meta_app_with_context(expr const & a) {
        return is_meta_app(a) && has_local_context(arg(a, 0));
    }

    static expr mk_lambda(name const & n, expr const & d, expr const & b) {
        return ::lean::mk_lambda(n, d, b);
    }

    /**
       \brief Create the term (fun (x_0 : types[0]) ... (x_{n-1} : types[n-1]) body)
    */
    expr mk_lambda(buffer<expr> const & types, expr const & body) {
        expr r = body;
        unsigned i = types.size();
        while (i > 0) {
            --i;
            r = mk_lambda(name(g_x_name, i), types[i], r);
        }
        return r;
    }

    /**
       \brief Return (f x_{num_vars - 1} ... x_0)
    */
    expr mk_app_vars(expr const & f, unsigned num_vars) {
        buffer<expr> args;
        args.push_back(f);
        unsigned i = num_vars;
        while (i > 0) {
            --i;
            args.push_back(mk_var(i));
        }
        return mk_app(args.size(), args.data());
    }

    /**
       \brief Auxiliary method for pushing a new constraint to the given constraint queue.
       If \c is_eq is true, then a equality constraint is created, otherwise a convertability constraint is created.
    */
    void push_new_constraint(cnstr_queue & q, bool is_eq, context const & new_ctx, expr const & new_a, expr const & new_b, trace const & new_tr) {
        if (is_eq)
            q.push_front(mk_eq_constraint(new_ctx, new_a, new_b, new_tr));
        else
            q.push_front(mk_convertible_constraint(new_ctx, new_a, new_b, new_tr));
    }

    void push_new_eq_constraint(cnstr_queue & q, context const & new_ctx, expr const & new_a, expr const & new_b, trace const & new_tr) {
        push_new_constraint(q, true, new_ctx, new_a, new_b, new_tr);
    }

    /**
       \brief Auxiliary method for pushing a new constraint to the current constraint queue.
       If \c is_eq is true, then a equality constraint is created, otherwise a convertability constraint is created.
    */
    void push_new_constraint(bool is_eq, context const & new_ctx, expr const & new_a, expr const & new_b, trace const & new_tr) {
        reset_quota();
        push_new_constraint(m_state.m_queue, is_eq, new_ctx, new_a, new_b, new_tr);
    }

    /**
       \brief Auxiliary method for pushing a new constraint to the current constraint queue.
       The new constraint is based on the constraint \c c. The constraint \c c may be a equality or convertability constraint.
       The update is justified by \c new_tr.
    */
    void push_updated_constraint(unification_constraint const & c, expr const & new_a, expr const & new_b, trace const & new_tr) {
        lean_assert(is_eq(c) || is_convertible(c));
        context const & ctx = get_context(c);
        if (is_eq(c))
            push_front(mk_eq_constraint(ctx, new_a, new_b, new_tr));
        else
            push_front(mk_convertible_constraint(ctx, new_a, new_b, new_tr));
    }

    /**
       \brief Auxiliary method for pushing a new constraint to the current constraint queue.
       The new constraint is based on the constraint \c c. The constraint \c c may be a equality or convertability constraint.
       The flag \c is_lhs says if the left-hand-side or right-hand-side are being updated with \c new_a.
       The update is justified by \c new_tr.
    */
    void push_updated_constraint(unification_constraint const & c, bool is_lhs, expr const & new_a, trace const & new_tr) {
        lean_assert(is_eq(c) || is_convertible(c));
        context const & ctx = get_context(c);
        if (is_eq(c)) {
            if (is_lhs)
                push_front(mk_eq_constraint(ctx, new_a, eq_rhs(c), new_tr));
            else
                push_front(mk_eq_constraint(ctx, eq_lhs(c), new_a, new_tr));
        } else {
            if (is_lhs)
                push_front(mk_convertible_constraint(ctx, new_a, convertible_to(c), new_tr));
            else
                push_front(mk_convertible_constraint(ctx, convertible_from(c), new_a, new_tr));
        }
    }

    /**
       \brief Auxiliary method for pushing a new constraint to the constraint queue.
       The new constraint is obtained from \c c by one or more normalization steps that produce \c new_a and \c new_b
    */
    void push_normalized_constraint(unification_constraint const & c, expr const & new_a, expr const & new_b) {
        push_updated_constraint(c, new_a, new_b, trace(new normalize_trace(c)));
    }

    /**
       \brief Assign \c v to \c m with justification \c tr in the current state.
    */
    void assign(expr const & m, expr const & v, context const & ctx, trace const & tr) {
        lean_assert(is_metavar(m));
        metavar_env & menv = m_state.m_menv;
        m_state.m_menv.assign(m, v, tr);
        if (menv.has_type(m)) {
            buffer<unification_constraint> ucs;
            expr tv = m_type_inferer(v, ctx, &menv, ucs);
            for (auto c : ucs)
                push_front(c);
            trace new_trace(new typeof_mvar_trace(ctx, m, menv.get_type(m), tv, tr));
            push_front(mk_convertible_constraint(ctx, tv, menv.get_type(m), new_trace));
        }
    }

    bool process(unification_constraint const & c) {
        m_quota--;
        switch (c.kind()) {
        case unification_constraint_kind::Eq:          return process_eq(c);
        case unification_constraint_kind::Convertible: return process_convertible(c);
        case unification_constraint_kind::Max:         return process_max(c);
        case unification_constraint_kind::Choice:      return process_choice(c);
        }
        lean_unreachable();
        return true;
    }

    bool process_eq(unification_constraint const & c) {
        return process_eq_convertible(get_context(c), eq_lhs(c), eq_rhs(c), c);
    }

    bool process_convertible(unification_constraint const & c) {
        return process_eq_convertible(get_context(c), convertible_from(c), convertible_to(c), c);
    }

    /**
       Process <tt>ctx |- a == b</tt> and <tt>ctx |- a << b</tt> when:
       1- \c a is an assigned metavariable
       2- \c a is a unassigned metavariable without a metavariable context.
       3- \c a is a unassigned metavariable of the form <tt>?m[lift:s:n, ...]</tt>, and \c b does not have
          a free variable in the range <tt>[s, s+n)</tt>.
       4- \c a is an application of the form <tt>(?m ...)</tt> where ?m is an assigned metavariable.
    */
    enum status { Processed, Failed, Continue };
    status process_metavar(unification_constraint const & c, expr const & a, expr const & b, bool is_lhs, bool allow_assignment) {
        if (is_metavar(a)) {
            if (is_assigned(a)) {
                // Case 1
                trace new_tr(new substitution_trace(c, get_mvar_trace(a)));
                push_updated_constraint(c, is_lhs, get_mvar_subst(a), new_tr);
                return Processed;
            } else if (!has_local_context(a)) {
                // Case 2
                if (has_metavar(b, a)) {
                    m_conflict = trace(new unification_failure_trace(c));
                    return Failed;
                } else if (allow_assignment) {
                    assign(a, b, get_context(c), trace(new assignment_trace(c)));
                    reset_quota();
                    return Processed;
                }
            } else {
                local_entry const & me = head(metavar_lctx(a));
                if (me.is_lift()) {
                    if (!has_free_var(b, me.s(), me.s() + me.n())) {
                        // Case 3
                        trace new_tr(new normalize_trace(c));
                        expr new_a = pop_meta_context(a);
                        expr new_b = lower_free_vars(b, me.s() + me.n(), me.n());
                        context new_ctx = get_context(c).remove(me.s(), me.n());
                        if (!is_lhs)
                            swap(new_a, new_b);
                        push_new_constraint(is_eq(c), new_ctx, new_a, new_b, new_tr);
                        return Processed;
                    } else if (is_var(b)) {
                        // Failure, there is no way to unify
                        // ?m[lift:s:n, ...] with a variable in [s, s+n]
                        m_conflict = trace(new unification_failure_trace(c));
                        return Failed;
                    }
                }
            }
        }

        if (is_app(a) && is_metavar(arg(a, 0)) && is_assigned(arg(a, 0))) {
            // Case 4
            trace new_tr(new substitution_trace(c, get_mvar_trace(arg(a, 0))));
            expr new_a = update_app(a, 0, get_mvar_subst(arg(a, 0)));
            push_updated_constraint(c, is_lhs, new_a, new_tr);
            return Processed;
        }
        return Continue;
    }

    trace mk_subst_trace(unification_constraint const & c, buffer<trace> const & subst_traces) {
        if (subst_traces.size() == 1) {
            return trace(new substitution_trace(c, subst_traces[0]));
        } else {
            return trace(new multi_substitution_trace(c, subst_traces.size(), subst_traces.data()));
        }
    }

    /**
       \brief Return true iff \c a contains instantiated variables. If this is the case,
       then constraint \c c is updated with a new \c a s.t. all metavariables of \c a
       are instantiated.

       \remark if \c is_lhs is true, then we are considering the left-hand-side of the constraint \c c.
    */
    bool instantiate_metavars(bool is_lhs, expr const & a, unification_constraint const & c) {
        lean_assert(is_eq(c) || is_convertible(c));
        lean_assert(!is_eq(c) || !is_lhs || is_eqp(eq_lhs(c), a));
        lean_assert(!is_eq(c) ||  is_lhs || is_eqp(eq_rhs(c), a));
        lean_assert(!is_convertible(c) || !is_lhs || is_eqp(convertible_from(c), a));
        lean_assert(!is_convertible(c) ||  is_lhs || is_eqp(convertible_to(c), a));
        if (has_assigned_metavar(a)) {
            metavar_env & menv = m_state.m_menv;
            buffer<trace> traces;
            auto f = [&](expr const & m, unsigned) -> expr {
                if (is_metavar(m) && menv.is_assigned(m)) {
                    trace t = menv.get_trace(m);
                    if (t)
                        traces.push_back(t);
                    return menv.get_subst(m);
                } else {
                    return m;
                }
            };
            expr new_a   = replace_fn<decltype(f)>(f)(a);
            trace new_tr = mk_subst_trace(c, traces);
            push_updated_constraint(c, is_lhs, new_a, new_tr);
            return true;
        } else {
            return false;
        }
    }

    /** \brief Unfold let-expression */
    void process_let(expr & a) {
        if (is_let(a))
            a = instantiate(let_body(a), let_value(a));
    }

    /** \brief Replace variables by their definition if the context contains it. */
    void process_var(context const & ctx, expr & a) {
        if (is_var(a)) {
            try {
                context_entry const & e = lookup(ctx, var_idx(a));
                if (e.get_body())
                    a = e.get_body();
            } catch (exception&) {
            }
        }
    }

    expr normalize(context const & ctx, expr const & a) {
        return m_normalizer(a, ctx, &(m_state.m_menv));
    }

    void process_app(context const & ctx, expr & a) {
        if (is_app(a)) {
            expr f = arg(a, 0);
            if (is_value(f) && m_use_normalizer) {
                // if f is a semantic attachment, we keep normalizing children from
                // left to right until the semantic attachment is applicable
                buffer<expr> new_args;
                new_args.append(num_args(a), &(arg(a, 0)));
                bool modified = false;
                expr r;
                for (unsigned i = 1; i < new_args.size(); i++) {
                    expr const & curr = new_args[i];
                    expr new_curr = normalize(ctx, curr);
                    if (curr != new_curr) {
                        modified = true;
                        new_args[i] = new_curr;
                        if (to_value(f).normalize(new_args.size(), new_args.data(), r)) {
                            a = r;
                            return;
                        }
                    }
                }
                if (modified) {
                    a = mk_app(new_args.size(), new_args.data());
                    return;
                }
            } else {
                process_let(f);
                process_var(ctx, f);
                f = head_beta_reduce(f);
                a = update_app(a, 0, f);
                a = head_beta_reduce(a);
            }
        }
    }

    void process_eq(context const & ctx, expr & a) {
        if (is_eq(a) && m_use_normalizer) {
            a = normalize(ctx, a);
        }
    }

    expr normalize_step(context const & ctx, expr const & a) {
        expr new_a = a;
        process_let(new_a);
        process_var(ctx, new_a);
        process_app(ctx, new_a);
        process_eq(ctx, new_a);
        return new_a;
    }

    int get_const_weight(expr const & a) {
        lean_assert(is_constant(a));
        object const & obj = m_env.find_object(const_name(a));
        if (obj && obj.is_definition() && !obj.is_opaque())
            return obj.get_weight();
        else
            return -1;
    }

    /**
        \brief Return a number >= 0 if \c a is a defined constant or the application of a defined constant.
        In this case, the value is the weight of the definition.
    */
    int get_unfolding_weight(expr const & a) {
        if (is_constant(a))
            return get_const_weight(a);
        else if (is_app(a) && is_constant(arg(a, 0)))
            return get_const_weight(arg(a, 0));
        else
            return -1;
    }

    expr unfold(expr const & a) {
        lean_assert(is_constant(a) || (is_app(a) && is_constant(arg(a, 0))));
        if (is_constant(a)) {
            return m_env.find_object(const_name(a)).get_value();
        } else {
            return update_app(a, 0, m_env.find_object(const_name(arg(a, 0))).get_value());
        }
    }

    bool normalize_head(expr a, expr b, unification_constraint const & c) {
        context const & ctx = get_context(c);
        bool modified = false;
        while (true) {
            check_interrupted(m_interrupted);
            expr new_a = normalize_step(ctx, a);
            expr new_b = normalize_step(ctx, b);
            if (new_a == a && new_b == b) {
                int w_a = get_unfolding_weight(a);
                int w_b = get_unfolding_weight(b);
                if (w_a >= 0 || w_b >= 0) {
                    if (w_a >= w_b)
                        new_a = unfold(a);
                    if (w_b >= w_a)
                        new_b = unfold(b);
                    if (new_a == a && new_b == b)
                        break;
                } else {
                    break;
                }
            }
            modified = true;
            a = new_a;
            b = new_b;
            if (a == b) {
                return true;
            }
        }
        if (modified) {
            push_normalized_constraint(c, a, b);
            return true;
        } else {
            return false;
        }
    }

    /** \brief Return true iff the variable with id \c vidx has a body/definition in the context \c ctx. */
    static bool has_body(context const & ctx, unsigned vidx) {
        try {
            context_entry const & e = lookup(ctx, vidx);
            if (e.get_body())
                return true;
        } catch (exception&) {
        }
        return false;
    }

    /**
       \brief Return true iff all arguments of the application \c a are variables that do
       not have a definition in \c ctx.
    */
    static bool are_args_vars(context const & ctx, expr const & a) {
        lean_assert(is_app(a));
        for (unsigned i = 1; i < num_args(a); i++) {
            if (!is_var(arg(a, i)))
                return false;
            if (has_body(ctx, var_idx(arg(a, i))))
                return false;
        }
        return true;
    }

    /**
        \brief Return true iff ctx |- a == b is a "simple" higher-order matching constraint. By simple, we mean
        a constraint of the form
               ctx |- (?m x) == c
        The constraint is solved by assigning ?m to (fun (x : T), c).
    */
    bool process_simple_ho_match(context const & ctx, expr const & a, expr const & b, bool is_lhs, unification_constraint const & c) {
        // Solve constraint of the form
        //    ctx |- (?m x) == c
        // using imitation
        if (is_eq(c) && is_meta_app(a) && are_args_vars(ctx, a) && closed(b)) {
            expr m = arg(a, 0);
            buffer<expr> types;
            for (unsigned i = 1; i < num_args(a); i++)
                types.push_back(lookup(ctx, var_idx(arg(a, i))).get_domain());
            trace new_trace(new destruct_trace(c));
            expr s = mk_lambda(types, b);
            if (!is_lhs)
                swap(m, s);
            push_front(mk_eq_constraint(ctx, m, s, new_trace));
            return true;
        } else {
            return false;
        }
    }

    /**
       \brief Auxiliary method for \c process_meta_app. Add new case-splits to new_cs
    */
    void process_meta_app_core(std::unique_ptr<generic_case_split> & new_cs, expr const & a, expr const & b, bool is_lhs, unification_constraint const & c) {
        lean_assert(is_meta_app(a));
        context const & ctx = get_context(c);
        metavar_env & menv  = m_state.m_menv;
        expr f_a            = arg(a, 0);
        lean_assert(is_metavar(f_a));
        unsigned num_a      = num_args(a);
        buffer<expr> arg_types;
        buffer<unification_constraint> ucs;
        for (unsigned i = 1; i < num_a; i++) {
            arg_types.push_back(m_type_inferer(arg(a, i), ctx, &menv, ucs));
            for (auto uc : ucs)
                push_front(uc);
        }
        // Add projections
        for (unsigned i = 1; i < num_a; i++) {
            // Assign f_a <- fun (x_1 : T_0) ... (x_{num_a-1} : T_{num_a-1}), x_i
            state new_state(m_state);
            trace new_assumption = mk_assumption();
            expr proj            = mk_lambda(arg_types, mk_var(num_a - i - 1));
            expr new_a           = arg(a, i);
            expr new_b           = b;
            if (!is_lhs)
                swap(new_a, new_b);
            push_new_constraint(new_state.m_queue, is_eq(c), ctx, new_a, new_b, new_assumption);
            push_new_eq_constraint(new_state.m_queue, ctx, f_a, proj, new_assumption);
            new_cs->push_back(new_state, new_assumption);
        }
        // Add imitation
        state new_state(m_state);
        trace new_assumption = mk_assumption();
        expr imitation;
        if (is_app(b)) {
            // Imitation for applications
            expr f_b          = arg(b, 0);
            unsigned num_b    = num_args(b);
            // Assign f_a <- fun (x_1 : T_0) ... (x_{num_a-1} : T_{num_a-1}), f_b (h_1 x_1 ... x_{num_a-1}) ... (h_{num_b-1} x_1 ... x_{num_a-1})
            // New constraints (h_i a_1 ... a_{num_a-1}) == arg(b, i)
            buffer<expr> imitation_args; // arguments for the imitation
            imitation_args.push_back(f_b);
            for (unsigned i = 1; i < num_b; i++) {
                expr h_i = new_state.m_menv.mk_metavar(ctx);
                imitation_args.push_back(mk_app_vars(h_i, num_a - 1));
                push_new_eq_constraint(new_state.m_queue, ctx, update_app(a, 0, h_i), arg(b, i), new_assumption);
            }
            imitation = mk_lambda(arg_types, mk_app(imitation_args.size(), imitation_args.data()));
        } else if (is_eq(b)) {
            // Imitation for equality
            // Assign f_a <- fun (x_1 : T_0) ... (x_{num_a-1} : T_{num_a-1}), (h_1 x_1 ... x_{num_a-1}) = (h_2 x_1 ... x_{num_a-1})
            // New constraints (h_1 a_1 ... a_{num_a-1}) == eq_lhs(b)
            //                 (h_2 a_1 ... a_{num_a-1}) == eq_rhs(b)
            expr h_1 = new_state.m_menv.mk_metavar(ctx);
            expr h_2 = new_state.m_menv.mk_metavar(ctx);
            push_new_eq_constraint(new_state.m_queue, ctx, update_app(a, 0, h_1), eq_lhs(b), new_assumption);
            push_new_eq_constraint(new_state.m_queue, ctx, update_app(a, 0, h_2), eq_rhs(b), new_assumption);
            imitation = mk_lambda(arg_types, mk_eq(mk_app_vars(h_1, num_a - 1), mk_app_vars(h_2, num_a - 1)));
        } else if (is_abstraction(b)) {
            // Imitation for lambdas and Pis
            // Assign f_a <- fun (x_1 : T_0) ... (x_{num_a-1} : T_{num_a-1}),
            //                        fun (x_b : (?h_1 x_1 ... x_{num_a-1})), (?h_2 x_1 ... x_{num_a-1} x_b)
            // New constraints (h_1 a_1 ... a_{num_a-1}) == abst_domain(b)
            //                 (h_2 a_1 ... a_{num_a-1} x_b) == abst_body(b)
            expr h_1 = new_state.m_menv.mk_metavar(ctx);
            expr h_2 = new_state.m_menv.mk_metavar(ctx);
            push_new_eq_constraint(new_state.m_queue, ctx, update_app(a, 0, h_1), abst_domain(b), new_assumption);
            push_new_eq_constraint(new_state.m_queue, extend(ctx, abst_name(b), abst_domain(b)),
                                   mk_app(update_app(a, 0, h_2), Var(0)), abst_body(b), new_assumption);
            imitation = mk_lambda(arg_types, update_abstraction(b, mk_app_vars(h_1, num_a - 1), mk_app_vars(h_2, num_a)));
        } else {
            // "Dumb imitation" aka the constant function
            // Assign f_a <- fun (x_1 : T_0) ... (x_{num_a-1} : T_{num_a-1}), b
            imitation = mk_lambda(arg_types, lift_free_vars(b, 0, num_a - 1));
        }
        lean_assert(imitation);
        push_new_eq_constraint(new_state.m_queue, ctx, f_a, imitation, new_assumption);
        new_cs->push_back(new_state, new_assumption);
    }

    /**
       \brief Process a constraint <tt>ctx |- a = b</tt> where \c a is of the form <tt>(?m ...)</tt>.
       We perform a "case split" using "projection" or "imitation". See Huet&Lang's paper on higher order matching
       for further details.
    */
    bool process_meta_app(expr const & a, expr const & b, bool is_lhs, unification_constraint const & c, bool flex_flex = false) {
        if (is_meta_app(a) && (flex_flex || !is_meta_app(b))) {
            std::unique_ptr<generic_case_split> new_cs(new generic_case_split(c, m_state));
            process_meta_app_core(new_cs, a, b, is_lhs, c);
            if (flex_flex && is_meta_app(b))
                process_meta_app_core(new_cs, b, a, !is_lhs, c);
            bool r = new_cs->next(*this);
            lean_assert(r);
            m_case_splits.push_back(std::move(new_cs));
            reset_quota();
            return r;
        } else {
            return false;
        }
    }

    /** \brief Return true if \c a is of the form ?m[inst:i t, ...] */
    bool is_metavar_inst(expr const & a) const {
        return is_metavar(a) && has_local_context(a) && head(metavar_lctx(a)).is_inst();
    }

    /**
       \brief Process a constraint <tt>ctx |- a == b</tt> where \c a is of the form <tt>?m[(inst:i t), ...]</tt>.
       We perform a "case split",
       Case 1) ?m[...] == #i and t == b
       Case 2) imitate b
    */
    bool process_metavar_inst(expr const & a, expr const & b, bool is_lhs, unification_constraint const & c) {
        if (is_metavar_inst(a) && !is_metavar_inst(b) && !is_meta_app(b)) {
            context const & ctx = get_context(c);
            local_context  lctx = metavar_lctx(a);
            unsigned i          = head(lctx).s();
            expr t              = head(lctx).v();
            std::unique_ptr<generic_case_split> new_cs(new generic_case_split(c, m_state));
            {
                // Case 1
                state new_state(m_state);
                trace new_assumption = mk_assumption();
                // add ?m[...] == #1
                push_new_eq_constraint(new_state.m_queue, ctx, pop_meta_context(a), mk_var(i), new_assumption);
                // add t == b (t << b)
                expr new_a = t;
                expr new_b = b;
                if (!is_lhs)
                    swap(new_a, new_b);
                push_new_constraint(new_state.m_queue, is_eq(c), ctx, new_a, new_b, new_assumption);
                new_cs->push_back(new_state, new_assumption);
            }
            {
                // Case 2
                state new_state(m_state);
                trace new_assumption = mk_assumption();
                expr  imitation;
                if (is_app(b)) {
                    // Imitation for applications b == f(s_1, ..., s_k)
                    // mname <- f(?h_1, ..., ?h_k)
                    expr f_b          = arg(b, 0);
                    unsigned num_b    = num_args(b);
                    buffer<expr> imitation_args;
                    imitation_args.push_back(f_b);
                    for (unsigned i = 1; i < num_b; i++)
                        imitation_args.push_back(new_state.m_menv.mk_metavar(ctx));
                    imitation = mk_app(imitation_args.size(), imitation_args.data());
                } else if (is_eq(b)) {
                    // Imitation for equality b == Eq(s1, s2)
                    // mname <- Eq(?h_1, ?h_2)
                    expr h_1 = new_state.m_menv.mk_metavar(ctx);
                    expr h_2 = new_state.m_menv.mk_metavar(ctx);
                    imitation = mk_eq(h_1, h_2);
                } else if (is_abstraction(b)) {
                    // Lambdas and Pis
                    // Imitation for Lambdas and Pis, b == Fun(x:T) B
                    // mname <- Fun (x:?h_1) ?h_2 x)
                    expr h_1 = new_state.m_menv.mk_metavar(ctx);
                    expr h_2 = new_state.m_menv.mk_metavar(ctx);
                    imitation = update_abstraction(b, h_1, mk_app(h_2, Var(0)));
                } else {
                    imitation = lift_free_vars(b, i, 1);
                }
                push_new_eq_constraint(new_state.m_queue, ctx, pop_meta_context(a), imitation, new_assumption);
                new_cs->push_back(new_state, new_assumption);
            }
            bool r = new_cs->next(*this);
            lean_assert(r);
            m_case_splits.push_back(std::move(new_cs));
            reset_quota();
            return r;
        } else {
            return false;
        }
    }

    /** \brief Process constraint of the form ctx |- a << ?m, where \c a is Type of Bool */
    bool process_lower(expr const & a, expr const & b, unification_constraint const & c) {
        if (is_convertible(c) && is_metavar(b) && (a == Bool || is_type(a))) {
            // Remark: in principle, there are infinite number of choices.
            // We approximate and only consider the most useful ones.
            trace new_tr(new destruct_trace(c));
            unification_constraint new_c;
            if (a == Bool) {
                expr choices[5] = { Bool, Type(), Type(level() + 1), TypeM, TypeU };
                new_c = mk_choice_constraint(get_context(c), b, 5, choices, new_tr);
            } else {
                expr choices[5] = { a, Type(ty_level(a) + 1), Type(ty_level(a) + 2), TypeM, TypeU };
                new_c = mk_choice_constraint(get_context(c), b, 5, choices, new_tr);
            }
            push_front(new_c);
            return true;
        } else {
            return false;
        }
    }

    bool process_eq_convertible(context const & ctx, expr const & a, expr const & b, unification_constraint const & c) {
        bool eq = is_eq(c);
        if (a == b) {
            return true;
        }

        status r;
        bool allow_assignment = eq; // at this point, we only assign metavariables if the constraint is an equational constraint.
        r = process_metavar(c, a, b, true, allow_assignment);
        if (r != Continue) { return r == Processed; }
        r = process_metavar(c, b, a, false, allow_assignment);
        if (r != Continue) { return r == Processed; }

        if (normalize_head(a, b, c)) { return true; }

        r = process_metavar(c, a, b, true, !is_type(b) && !is_meta(b));
        if (r != Continue) { return r == Processed; }
        r = process_metavar(c, b, a, false, !is_type(a) && !is_meta(a) && a != Bool);
        if (r != Continue) { return r == Processed; }

        if (process_simple_ho_match(ctx, a, b, true, c) ||
            process_simple_ho_match(ctx, b, a, false, c))
            return true;

        if (!eq && a == Bool && is_type(b))
            return true;

        if (a.kind() == b.kind()) {
            switch (a.kind()) {
            case expr_kind::Constant: case expr_kind::Var: case expr_kind::Value:
                if (a == b) {
                    return true;
                } else {
                    m_conflict = trace(new unification_failure_trace(c));
                    return false;
                }
            case expr_kind::Type:
                if ((!eq && m_env.is_ge(ty_level(b), ty_level(a))) || (eq && a == b)) {
                    return true;
                } else {
                    m_conflict = trace(new unification_failure_trace(c));
                    return false;
                }
            case expr_kind::Eq: {
                trace new_trace(new destruct_trace(c));
                push_front(mk_eq_constraint(ctx, eq_lhs(a), eq_lhs(b), new_trace));
                push_front(mk_eq_constraint(ctx, eq_rhs(a), eq_rhs(b), new_trace));
                return true;
            }
            case expr_kind::Pi: {
                trace new_trace(new destruct_trace(c));
                push_front(mk_eq_constraint(ctx, abst_domain(a), abst_domain(b), new_trace));
                context new_ctx = extend(ctx, abst_name(a), abst_domain(a));
                if (eq)
                    push_front(mk_eq_constraint(new_ctx, abst_body(a), abst_body(b), new_trace));
                else
                    push_front(mk_convertible_constraint(new_ctx, abst_body(a), abst_body(b), new_trace));
                return true;
            }
            case expr_kind::Lambda: {
                trace new_trace(new destruct_trace(c));
                push_front(mk_eq_constraint(ctx, abst_domain(a), abst_domain(b), new_trace));
                context new_ctx = extend(ctx, abst_name(a), abst_domain(a));
                push_front(mk_eq_constraint(new_ctx, abst_body(a), abst_body(b), new_trace));
                return true;
            }
            case expr_kind::App:
                if (!is_meta_app(a) && !is_meta_app(b)) {
                    if (num_args(a) == num_args(b)) {
                        trace new_trace(new destruct_trace(c));
                        for (unsigned i = 0; i < num_args(a); i++)
                            push_front(mk_eq_constraint(ctx, arg(a, i), arg(b, i), new_trace));
                        return true;
                    } else {
                        return false;
                    }
                }
                break;
            case expr_kind::Let:
                lean_unreachable();
                return true;
            default:
                break;
            }
        }

        if (instantiate_metavars(true, a, c) ||
            instantiate_metavars(false, b, c)) {
            return true;
        }

        if (a.kind() != b.kind() && !has_metavar(a) && !has_metavar(b)) {
            m_conflict = trace(new unification_failure_trace(c));
            return false;
        }

        if (m_quota < 0) {
            // process expensive cases
            if (process_meta_app(a, b, true, c) || process_meta_app(b, a, false, c))
                return true;
            if (process_metavar_inst(a, b, true, c) || process_metavar_inst(b, a, false, c))
                return true;
        }

        if (m_quota < - static_cast<int>(m_state.m_queue.size())) {
            // process very expensive cases
            if (process_lower(a, b, c))
                return true;
            if (process_meta_app(a, b, true, c, true))
                return true;
        }

        // std::cout << "Postponed: "; display(std::cout, c);
        push_back(c);

        return true;
    }


    bool process_max(unification_constraint const &) {
        // TODO(Leo)
        return true;
    }

    bool process_choice(unification_constraint const & c) {
        std::unique_ptr<case_split> new_cs(new choice_case_split(c, m_state));
        bool r = new_cs->next(*this);
        lean_assert(r);
        m_case_splits.push_back(std::move(new_cs));
        return r;
    }

    void resolve_conflict() {
        lean_assert(m_conflict);

        std::cout << "Resolve conflict, num case_splits: " << m_case_splits.size() << "\n";
        formatter fmt = mk_simple_formatter();
        std::cout << m_conflict.pp(fmt, options(), nullptr, true) << "\n";

        while (!m_case_splits.empty()) {
            std::unique_ptr<case_split> & d = m_case_splits.back();
            // std::cout << "Assumption " << d->m_curr_assumption.pp(fmt, options(), nullptr, true) << "\n";
            if (depends_on(m_conflict, d->m_curr_assumption)) {
                d->m_failed_traces.push_back(m_conflict);
                if (d->next(*this)) {
                    m_conflict = trace();
                    reset_quota();
                    return;
                }
            }
            m_case_splits.pop_back();
        }
        throw elaborator_exception(m_conflict);
    }

    bool next_choice_case(choice_case_split & s) {
        unification_constraint & choice = s.m_choice;
        unsigned idx = s.m_idx;
        if (idx < choice_size(choice)) {
            s.m_idx++;
            s.m_curr_assumption = mk_assumption();
            m_state = s.m_prev_state;
            push_front(mk_eq_constraint(get_context(choice), choice_mvar(choice), choice_ith(choice, idx), s.m_curr_assumption));
            return true;
        } else {
            m_conflict = trace(new unification_failure_by_cases_trace(choice, s.m_failed_traces.size(), s.m_failed_traces.data()));
            return false;
        }
    }

    bool next_generic_case(generic_case_split & s) {
        unsigned idx = s.m_idx;
        unsigned sz  = s.m_states.size();
        if (idx < sz) {
            s.m_idx++;
            s.m_curr_assumption = s.m_assumptions[sz - idx - 1];
            m_state             = s.m_states[sz - idx - 1];
            return true;
        } else {
            m_conflict = trace(new unification_failure_by_cases_trace(s.m_constraint, s.m_failed_traces.size(), s.m_failed_traces.data()));
            return false;
        }
    }

    bool next_plugin_case(plugin_case_split & s) {
        try {
            s.m_curr_assumption = mk_assumption();
            std::pair<metavar_env, list<unification_constraint>> r = s.m_alternatives->next(s.m_curr_assumption);
            m_state.m_queue     = s.m_prev_state.m_queue;
            m_state.m_menv      = r.first;
            for (auto c : r.second) {
                push_front(c);
            }
            return true;
        } catch (exception & ex) {
            m_conflict = trace(new unification_failure_by_cases_trace(s.m_constraint, s.m_failed_traces.size(), s.m_failed_traces.data()));
            return false;
        }
    }

public:
    imp(environment const & env, metavar_env const & menv, unsigned num_cnstrs, unification_constraint const * cnstrs,
        options const & opts, std::shared_ptr<synthesizer> const & s, std::shared_ptr<elaborator_plugin> const & p):
        m_env(env),
        m_type_inferer(env),
        m_normalizer(env),
        m_state(menv, num_cnstrs, cnstrs),
        m_synthesizer(s),
        m_plugin(p) {
        set_options(opts);
        m_next_id     = 0;
        m_quota       = 0;
        m_interrupted = false;
        m_first       = true;
        display(std::cout);
    }

    substitution next() {
        check_interrupted(m_interrupted);
        if (m_conflict)
            throw elaborator_exception(m_conflict);
        if (!m_case_splits.empty()) {
            buffer<trace> assumptions;
            for (std::unique_ptr<case_split> const & cs : m_case_splits)
                assumptions.push_back(cs->m_curr_assumption);
            m_conflict = trace(new next_solution_trace(assumptions.size(), assumptions.data()));
            resolve_conflict();
        } else if (m_first) {
            m_first = false;
        } else {
            // this is not the first run, and there are no case-splits
            m_conflict = trace(new next_solution_trace(0, nullptr));
            throw elaborator_exception(m_conflict);
        }
        reset_quota();
        while (true) {
            check_interrupted(m_interrupted);
            cnstr_queue & q = m_state.m_queue;
            if (q.empty() || m_quota < - static_cast<int>(q.size()) - 10) {
                name m = find_unassigned_metavar();
                std::cout << "Queue is empty\n"; display(std::cout);
                if (m) {
                    // TODO(Leo)
                    // erase the following line, and implement interface with synthesizer
                    return m_state.m_menv.get_substitutions();
                } else {
                    return m_state.m_menv.get_substitutions();
                }
            } else {
                unification_constraint c = q.front();
                // std::cout << "Processing, quota: " << m_quota << ", depth: " << m_case_splits.size() << " "; display(std::cout, c);
                q.pop_front();
                if (!process(c)) {
                    resolve_conflict();
                }
            }
        }
    }

    void interrupt() {
        m_interrupted = true;
        m_type_inferer.set_interrupt(true);
        m_normalizer.set_interrupt(true);
    }

    void display(std::ostream & out, unification_constraint const & c) const {
        formatter fmt = mk_simple_formatter();
        out << c.pp(fmt, options(), nullptr, false) << "\n";
    }

    void display(std::ostream & out) const {
        m_state.m_menv.get_substitutions().for_each([&](name const & m, expr const & e) {
                out << m << " <- " << e << "\n";
            });
        for (auto c : m_state.m_queue)
            display(out, c);
    }
};

elaborator::elaborator(environment const & env,
                       metavar_env const & menv,
                       unsigned num_cnstrs,
                       unification_constraint const * cnstrs,
                       options const & opts,
                       std::shared_ptr<synthesizer> const & s,
                       std::shared_ptr<elaborator_plugin> const & p):
    m_ptr(new imp(env, menv, num_cnstrs, cnstrs, opts, s, p)) {
}

elaborator::elaborator(environment const & env,
                       metavar_env const & menv,
                       context const & ctx, expr const & lhs, expr const & rhs):
    elaborator(env, menv, { mk_eq_constraint(ctx, lhs, rhs, trace()) }) {
}

elaborator::~elaborator() {
}

substitution elaborator::next() {
    return m_ptr->next();
}

void elaborator::interrupt() {
    m_ptr->interrupt();
}
}