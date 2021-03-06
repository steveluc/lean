/*
Copyright (c) 2013 Microsoft Corporation. All rights reserved.
Released under Apache 2.0 license as described in the file LICENSE.

Author: Leonardo de Moura
*/
#pragma once
#include "kernel/expr.h"

namespace lean {
class ro_metavar_env;
/**
   \brief Replace the free variables with indices 0, ..., n-1 with s[n-1], ..., s[0] in e.

   \pre s[0], ..., s[n-1] must be closed expressions (i.e., no free variables).

   \remark When the parameter menv is not none, this function will minimize the use
   of the local entry inst in metavariables occurring in \c e.
*/
expr instantiate_with_closed(expr const & e, unsigned n, expr const * s, optional<ro_metavar_env> const & menv);
expr instantiate_with_closed(expr const & e, unsigned n, expr const * s, ro_metavar_env const & menv);
expr instantiate_with_closed(expr const & e, unsigned n, expr const * s);
expr instantiate_with_closed(expr const & e, std::initializer_list<expr> const & l);
expr instantiate_with_closed(expr const & e, expr const & s, optional<ro_metavar_env> const & menv);
expr instantiate_with_closed(expr const & e, expr const & s, ro_metavar_env const & menv);
expr instantiate_with_closed(expr const & e, expr const & s);

/**
   \brief Replace the free variables with indices 0, ..., n-1 with s[n-1], ..., s[0] in e.

   \remark When the parameter menv is not none, this function will minimize the use
   of the local entry inst in metavariables occurring in \c e.
*/
expr instantiate(expr const & e, unsigned n, expr const * s, optional<ro_metavar_env> const & menv);
expr instantiate(expr const & e, unsigned n, expr const * s, ro_metavar_env const & menv);
expr instantiate(expr const & e, unsigned n, expr const * s);
expr instantiate(expr const & e, std::initializer_list<expr> const & l);
/**
   \brief Replace free variable \c i with \c s in \c e.
*/
expr instantiate(expr const & e, unsigned i, expr const & s, optional<ro_metavar_env> const & menv);
expr instantiate(expr const & e, unsigned i, expr const & s, ro_metavar_env const & menv);
expr instantiate(expr const & e, unsigned i, expr const & s);
/**
   \brief Replace free variable \c 0 with \c s in \c e.
*/
expr instantiate(expr const & e, expr const & s, optional<ro_metavar_env> const & menv);
expr instantiate(expr const & e, expr const & s, ro_metavar_env const & menv);
expr instantiate(expr const & e, expr const & s);

expr apply_beta(expr f, unsigned num_args, expr const * args, optional<ro_metavar_env> const & menv);
expr apply_beta(expr f, unsigned num_args, expr const * args, ro_metavar_env const & menv);
expr apply_beta(expr f, unsigned num_args, expr const * args);

bool is_head_beta(expr const & t);

expr head_beta_reduce(expr const & t, optional<ro_metavar_env> const & menv);
expr head_beta_reduce(expr const & t, ro_metavar_env const & menv);
expr head_beta_reduce(expr const & t);

expr beta_reduce(expr t, optional<ro_metavar_env> const & menv);
expr beta_reduce(expr t, ro_metavar_env const & menv);
expr beta_reduce(expr t);
}
