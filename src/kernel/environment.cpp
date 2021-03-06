/*
Copyright (c) 2013 Microsoft Corporation. All rights reserved.
Released under Apache 2.0 license as described in the file LICENSE.

Author: Leonardo de Moura
*/
#include <cstdlib>
#include <algorithm>
#include <vector>
#include <tuple>
#include <fstream>
#include <string>
#include <utility>
#include "util/thread.h"
#include "util/safe_arith.h"
#include "util/realpath.h"
#include "util/sstream.h"
#include "util/lean_path.h"
#include "util/flet.h"
#include "kernel/for_each_fn.h"
#include "kernel/find_fn.h"
#include "kernel/kernel_exception.h"
#include "kernel/environment.h"
#include "kernel/threadsafe_environment.h"
#include "kernel/type_checker.h"
#include "kernel/normalizer.h"
#include "kernel/universe_constraints.h"
#include "version.h"

namespace lean {
class set_opaque_command : public neutral_object_cell {
    name m_obj_name;
    bool m_opaque;
public:
    set_opaque_command(name const & n, bool opaque):m_obj_name(n), m_opaque(opaque) {}
    virtual ~set_opaque_command() {}
    virtual char const * keyword() const { return "set_opaque"; }
    virtual void write(serializer & s) const { s << "Opa" << m_obj_name << m_opaque; }
    name const & get_obj_name() const { return m_obj_name; }
    bool get_flag() const { return m_opaque; }
};
static void read_set_opaque(environment const & env, io_state const &, deserializer & d) {
    name n = read_name(d);
    bool o = d.read_bool();
    env->set_opaque(n, o);
}
static object_cell::register_deserializer_fn set_opaque_ds("Opa", read_set_opaque);

bool is_set_opaque(object const & obj) {
    return dynamic_cast<set_opaque_command const *>(obj.cell());
}

name const & get_set_opaque_id(object const & obj) {
    lean_assert(is_set_opaque(obj));
    return static_cast<set_opaque_command const *>(obj.cell())->get_obj_name();
}

bool get_set_opaque_flag(object const & obj) {
    lean_assert(is_set_opaque(obj));
    return static_cast<set_opaque_command const *>(obj.cell())->get_flag();
}

class import_command : public neutral_object_cell {
    std::string m_mod_name;
public:
    import_command(std::string const & n):m_mod_name(n) {}
    virtual ~import_command() {}
    virtual char const * keyword() const { return "import"; }
    virtual void write(serializer & s) const { s << "import" << m_mod_name; }
    std::string const & get_module() const { return m_mod_name; }
};
static void read_import(environment const & env, io_state const & ios, deserializer & d) {
    std::string n = d.read_string();
    env->import(n, ios);
}
static object_cell::register_deserializer_fn import_ds("import", read_import);

class end_import_mark : public neutral_object_cell {
public:
    end_import_mark() {}
    virtual ~end_import_mark() {}
    virtual char const * keyword() const { return "EndImport"; }
    virtual void write(serializer &) const {}
};

// For Importing builtin modules
class begin_import_mark : public neutral_object_cell {
public:
    begin_import_mark() {}
    virtual ~begin_import_mark() {}
    virtual char const * keyword() const { return "BeginImport"; }
    virtual void write(serializer &) const {}
};

bool is_begin_import(object const & obj) {
    return dynamic_cast<import_command const*>(obj.cell());
}

optional<std::string> get_imported_module(object const & obj) {
    if (is_begin_import(obj)) {
        return optional<std::string>(static_cast<import_command const*>(obj.cell())->get_module());
    } else {
        return optional<std::string>();
    }
}

bool is_begin_builtin_import(object const & obj) {
    return dynamic_cast<begin_import_mark const*>(obj.cell());
}

bool is_end_import(object const & obj) {
    return dynamic_cast<end_import_mark const*>(obj.cell());
}

class extension_factory {
    std::vector<environment_cell::mk_extension> m_makers;
    mutex                                       m_makers_mutex;
public:
    unsigned register_extension(environment_cell::mk_extension mk) {
        lock_guard<mutex> lock(m_makers_mutex);
        unsigned r = m_makers.size();
        m_makers.push_back(mk);
        return r;
    }

    std::unique_ptr<environment_extension> mk(unsigned extid) {
        lock_guard<mutex> lock(m_makers_mutex);
        return m_makers[extid]();
    }
};

static std::unique_ptr<extension_factory> g_extension_factory;
static extension_factory & get_extension_factory() {
    if (!g_extension_factory)
        g_extension_factory.reset(new extension_factory());
    return *g_extension_factory;
}

unsigned environment_cell::register_extension(mk_extension mk) {
    return get_extension_factory().register_extension(mk);
}

environment environment_cell::env() const {
    lean_assert(!m_this.expired()); // it is not possible to expire since it is a reference to this object
    lean_assert(this == m_this.lock().get());
    return environment(m_this.lock());
}

environment environment_cell::parent() const {
    lean_assert(has_parent());
    return environment(m_parent);
}

environment environment_cell::mk_child() const {
    return environment(m_this.lock(), true);
}

environment_extension & environment_cell::get_extension_core(unsigned extid) {
    if (extid >= m_extensions.size())
        m_extensions.resize(extid+1);
    if (!m_extensions[extid]) {
        std::unique_ptr<environment_extension> ext = get_extension_factory().mk(extid);
        ext->m_extid = extid;
        ext->m_env   = this;
        m_extensions[extid].swap(ext);
    }
    return *(m_extensions[extid].get());
}

environment_extension const & environment_cell::get_extension_core(unsigned extid) const {
    return const_cast<environment_cell *>(this)->get_extension_core(extid);
}

unsigned environment_cell::get_max_weight(expr const & e) {
    unsigned w = 0;
    auto proc = [&](expr const & c, unsigned) {
        if (is_constant(c)) {
            optional<object> obj = get_object_core(const_name(c));
            if (obj)
                w = std::max(w, obj->get_weight());
        }
        return true;
        };
    for_each_fn<decltype(proc)> visitor(proc);
    visitor(e);
    return w;
}

/** \brief Throw exception if environment or its ancestors already have an object with the given name. */
void environment_cell::check_name_core(name const & n) {
    if (has_parent())
        m_parent->check_name_core(n);
    if (m_object_dictionary.find(n) != m_object_dictionary.end())
        throw already_declared_exception(env(), n);
}

void environment_cell::check_name(name const & n) {
    if (has_children())
        throw read_only_environment_exception(env());
    check_name_core(n);
}

/** \brief Store new named object inside internal data-structures */
void environment_cell::register_named_object(object const & new_obj) {
    m_objects.push_back(new_obj);
    m_object_dictionary.insert(std::make_pair(new_obj.get_name(), new_obj));
}

/**
   \brief Return the object named \c n in the environment or its
   ancestors. Return null object if there is no object with the
   given name.
*/
optional<object> environment_cell::get_object_core(name const & n) const {
    auto it = m_object_dictionary.find(n);
    if (it == m_object_dictionary.end()) {
        if (has_parent())
            return m_parent->get_object_core(n);
        else
            return none_object();
    } else {
        return some_object(it->second);
    }
}

object environment_cell::get_object(name const & n) const {
    optional<object> obj = get_object_core(n);
    if (obj) {
        return *obj;
    } else {
        throw unknown_object_exception(env(), n);
    }
}

class universes {
public:
    std::vector<level>                  m_uvars;
    universe_constraints                m_constraints;
};

universes & environment_cell::get_rw_universes() {
    if (!m_universes) {
        lean_assert(has_parent());
        m_universes.reset(new universes(m_parent->get_rw_universes()));
    }
    return *m_universes;
}

universes const & environment_cell::get_ro_universes() const {
    if (m_universes) {
        return *m_universes;
    } else {
        lean_assert(has_parent());
        return m_parent->get_ro_universes();
    }
}

universe_constraints & environment_cell::get_rw_ucs() {
    return get_rw_universes().m_constraints;
}

universe_constraints const & environment_cell::get_ro_ucs() const {
    return get_ro_universes().m_constraints;
}

optional<int> environment_cell::get_universe_distance(name const & u1, name const & u2) const {
    return get_ro_ucs().get_distance(u1, u2);
}

/** \brief Return true iff l1 >= l2 + k by asserted universe constraints. */
bool environment_cell::is_ge(level const & l1, level const & l2, int k) const {
    if (l1 == l2)
        return k <= 0;
    switch (kind(l2)) {
    case level_kind::UVar:
        switch (kind(l1)) {
        case level_kind::UVar: return get_ro_ucs().is_implied(uvar_name(l1), uvar_name(l2), k);
        case level_kind::Lift: return is_ge(lift_of(l1), l2, safe_sub(k, lift_offset(l1)));
        case level_kind::Max:  return std::any_of(max_begin_levels(l1), max_end_levels(l1), [&](level const & l) { return is_ge(l, l2, k); });
        }
    case level_kind::Lift: return is_ge(l1, lift_of(l2), safe_add(k, lift_offset(l2)));
    case level_kind::Max:  return std::all_of(max_begin_levels(l2), max_end_levels(l2), [&](level const & l) { return is_ge(l1, l, k); });
    }
    lean_unreachable(); // LCOV_EXCL_LINE
}

/** \brief Return true iff l1 >= l2 is implied by asserted universe constraints. */
bool environment_cell::is_ge(level const & l1, level const & l2) const {
    return is_ge(l1, l2, 0);
}

/** \brief Add a new universe variable */
level environment_cell::add_uvar_core(name const & n) {
    check_name(n);
    universes & u = get_rw_universes();
    u.m_constraints.add_var(n);
    level r(n);
    u.m_uvars.push_back(r);
    return r;
}

/**
   \brief Add all basic constraints implied by n >= l + k

   A basic constraint is a constraint of the form u >= v + k
   where u and v are universe variables.
*/
void environment_cell::add_constraints(name const & n, level const & l, int k) {
    switch (kind(l)) {
    case level_kind::UVar: get_rw_ucs().add_constraint(n, uvar_name(l), k); return;
    case level_kind::Lift: add_constraints(n, lift_of(l), safe_add(k, lift_offset(l))); return;
    case level_kind::Max:  std::for_each(max_begin_levels(l), max_end_levels(l), [&](level const & l1) { add_constraints(n, l1, k); }); return;
    }
    lean_unreachable(); // LCOV_EXCL_LINE
}

/**
   \brief Check if n >= l + k is consistent with the existing constraints.
*/
void environment_cell::check_consistency(name const & n, level const & l, int k) const {
    switch (kind(l)) {
    case level_kind::UVar:
        if (!get_ro_ucs().is_consistent(n, uvar_name(l), k))
            throw kernel_exception(env(), sstream() << "universe constraint inconsistency: " << n << " >= " << l << " + " << k);
        if (get_ro_ucs().overflows(n, uvar_name(l), k))
            throw kernel_exception(env(), sstream() << "universe constraint produces an integer overflow: " << n << " >= " << l << " + " << k);
        return;
    case level_kind::Lift: check_consistency(n, lift_of(l), safe_add(k, lift_offset(l))); return;
    case level_kind::Max:  std::for_each(max_begin_levels(l), max_end_levels(l), [&](level const & l1) { check_consistency(n, l1, k); }); return;
    }
    lean_unreachable(); // LCOV_EXCL_LINE
}

/** \brief Add a new universe variable with constraint n >= l */
level environment_cell::add_uvar_cnstr(name const & n, level const & l) {
    if (has_children())
        throw read_only_environment_exception(env());
    level r;
    auto const & uvs = get_ro_universes().m_uvars;
    auto it = std::find_if(uvs.begin(), uvs.end(), [&](level const & l) { return uvar_name(l) == n; });
    check_consistency(n, l, 0);
    if (it == uvs.end()) {
        r = add_uvar_core(n);
    } else {
        // universe n already exists, we must check consistency of the new constraint.
        r = *it;
    }
    m_objects.push_back(mk_uvar_cnstr(n, l));
    add_constraints(n, l, 0);
    return r;
}

/**
   \brief Return the universe variable with given name. Throw an
   exception if the environment and its ancestors do not
   contain a universe variable named \c n.
*/
level environment_cell::get_uvar(name const & n) const {
    auto const & uvs = get_ro_universes().m_uvars;
    auto it = std::find_if(uvs.begin(), uvs.end(), [&](level const & l) { return uvar_name(l) == n; });
    if (it == uvs.end())
        throw unknown_universe_variable_exception(env(), n);
    else
        return *it;
}

/**
   \brief Initialize the set of universe variables with bottom
*/
void environment_cell::init_uvars() {
    m_universes.reset(new universes());
    universes & u = get_rw_universes();
    level bottom;
    u.m_uvars.push_back(bottom);
    u.m_constraints.add_var(uvar_name(bottom));
}

/**
   The kernel should *not* accept expressions containing cached types.
   Reason: Cached types may introduce unsoundness.
   For example, in the environment env, the constant x may have type T.
   Now suppose we are trying to add a new definition D that contains x,
   and x is associated with a cached type T'. The cached type may allow
   us to accept a definition that is type incorrect with respect to env.
*/
void environment_cell::check_no_cached_type(expr const & e) {
    if (find(e, [](expr const & a) { return is_constant(a) && const_type(a); }))
        throw kernel_exception(env(), "expression has a constant with a cached type, this is a bug in one of Lean tactics and/or solvers"); // LCOV_EXCL_LINE
}

/**
   \brief Throw an exception if \c t is not a type or type of \c
   v is not convertible to \c t.
*/
void environment_cell::check_type(name const & n, expr const & t, expr const & v) {
    if (m_type_check) {
        m_type_checker->check_type(t);
        expr v_t = m_type_checker->check(v);
        if (!m_type_checker->is_convertible(v_t, t))
            throw def_type_mismatch_exception(env(), n, t, v, v_t);
    }
}

/** \brief Throw exception if it is not a valid new definition */
void environment_cell::check_new_definition(name const & n, expr const & t, expr const & v) {
    check_name(n);
    check_type(n, t, v);
}

/** \brief Add a new builtin value to this environment */
void environment_cell::add_builtin(expr const & v) {
    if (!is_value(v))
        throw invalid_builtin_value_declaration(env(), v); // LCOV_EXCL_LINE
    name const & n = to_value(v).get_name();
    check_name(n);
    name const & u = to_value(v).get_unicode_name();
    check_name(u);
    register_named_object(mk_builtin(v));
    if (u != n) {
        auxiliary_section([&]() {
                add_definition(u, to_value(v).get_type(), mk_constant(n), false);
            });
    }
}

/** \brief Add a new builtin value set to this environment */
void environment_cell::add_builtin_set(expr const & r) {
    if (!is_value(r))
        throw invalid_builtin_value_declaration(env(), r); // LCOV_EXCL_LINE
    check_name(to_value(r).get_name());
    register_named_object(mk_builtin_set(r));
}

/** \brief Add new definition. */
void environment_cell::add_definition(name const & n, expr const & t, expr const & v, bool opaque) {
    check_no_cached_type(t);
    check_no_cached_type(v);
    check_new_definition(n, t, v);
    unsigned w = get_max_weight(v) + 1;
    register_named_object(mk_definition(n, t, v, w));
    if (opaque)
        set_opaque(n, opaque);
}

/**
   \brief Add new definition.
   The type of the new definition is the type of \c v.
*/
void environment_cell::add_definition(name const & n, expr const & v, bool opaque) {
    check_no_cached_type(v);
    check_name(n);
    expr v_t;
    if (m_type_check)
        v_t = m_type_checker->check(v);
    else
        v_t = m_type_checker->infer_type(v);
    unsigned w = get_max_weight(v) + 1;
    register_named_object(mk_definition(n, v_t, v, w));
    if (opaque)
        set_opaque(n, opaque);
}

/** \brief Add new theorem. */
void environment_cell::add_theorem(name const & n, expr const & t, expr const & v) {
    check_no_cached_type(t);
    check_no_cached_type(v);
    check_new_definition(n, t, v);
    register_named_object(mk_theorem(n, t, v));
}

void environment_cell::set_opaque(name const & n, bool opaque) {
    auto obj = find_object(n);
    if (!obj || !obj->is_definition())
        throw kernel_exception(env(), sstream() << "set_opaque failed, '" << n << "' is not a definition");
    obj->set_opaque(opaque);
    add_neutral_object(new set_opaque_command(n, opaque));
}

/** \brief Add new axiom. */
void environment_cell::add_axiom(name const & n, expr const & t) {
    check_no_cached_type(t);
    check_name(n);
    if (m_type_check)
        m_type_checker->check_type(t);
    register_named_object(mk_axiom(n, t));
}

/** \brief Add new variable. */
void environment_cell::add_var(name const & n, expr const & t) {
    check_no_cached_type(t);
    check_name(n);
    if (m_type_check)
        m_type_checker->check_type(t);
    register_named_object(mk_var_decl(n, t));
}

void environment_cell::add_neutral_object(neutral_object_cell * o) {
    m_objects.push_back(mk_neutral(o));
}

unsigned environment_cell::get_num_objects(bool local) const {
    if (local || !has_parent()) {
        return m_objects.size();
    } else {
        return m_objects.size() + m_parent->get_num_objects(false);
    }
}

object const & environment_cell::get_object(unsigned i, bool local) const {
    if (local || !has_parent()) {
        return m_objects[i];
    } else {
        unsigned num_parent_objects = m_parent->get_num_objects(false);
        if (i >= num_parent_objects)
            return m_objects[i - num_parent_objects];
        else
            return m_parent->get_object(i, false);
    }
}

expr environment_cell::type_check(expr const & e, context const & ctx) const {
    return m_type_checker->check(e, ctx);
}

expr environment_cell::infer_type(expr const & e, context const & ctx) const {
    return m_type_checker->infer_type(e, ctx);
}

expr environment_cell::normalize(expr const & e, context const & ctx, bool unfold_opaque) const {
    return m_type_checker->get_normalizer()(e, ctx, unfold_opaque);
}

bool environment_cell::is_proposition(expr const & e, context const & ctx) const {
    return m_type_checker->is_proposition(e, ctx);
}

bool environment_cell::already_imported(name const & n) const {
    if (m_imported_modules.find(n) != m_imported_modules.end())
        return true;
    else if (has_parent())
        return m_parent->already_imported(n);
    else
        return false;
}

bool environment_cell::mark_imported_core(name n) {
    if (already_imported(n)) {
        return false;
    } else if (has_children()) {
        throw read_only_environment_exception(env());
    } else {
        m_imported_modules.insert(n);
        return true;
    }
}

bool environment_cell::mark_imported(char const * fname) {
    return mark_imported_core(name(realpath(fname)));
}

void environment_cell::auxiliary_section(std::function<void()> fn) {
    add_neutral_object(new begin_import_mark());
    try {
        fn();
        add_neutral_object(new end_import_mark());
    } catch (...) {
        add_neutral_object(new end_import_mark());
        throw;
    }
}

void environment_cell::set_trusted_imported(bool flag) {
    m_trust_imported = flag;
}

static char const * g_olean_header   = "oleanfile";
static char const * g_olean_end_file = "EndFile";
void environment_cell::export_objects(std::string const & fname) {
    std::ofstream out(fname, std::ofstream::binary);
    serializer s(out);
    s << g_olean_header << LEAN_VERSION_MAJOR << LEAN_VERSION_MINOR;
    auto it  = begin_objects();
    auto end = end_objects();
    unsigned num_imports = 0;
    for (; it != end; ++it) {
        object const & obj = *it;
        if (dynamic_cast<import_command const*>(obj.cell())) {
            if (num_imports == 0)
                obj.write(s);
            num_imports++;
        } else if (dynamic_cast<end_import_mark const*>(obj.cell())) {
            lean_assert(num_imports > 0);
            num_imports--;
        } else if (dynamic_cast<begin_import_mark const*>(obj.cell())) {
            num_imports++;
        } else if (num_imports == 0) {
            obj.write(s);
        }
    }
    s << g_olean_end_file;
}

bool environment_cell::load_core(std::string const & fname, io_state const & ios, optional<std::string> const & mod_name) {
    if (!mod_name || mark_imported_core(fname)) {
        std::ifstream in(fname, std::ifstream::binary);
        if (!in.good())
            throw exception(sstream() << "failed to open file '" << fname << "'");
        deserializer d(in);
        std::string header;
        d >> header;
        if (header != g_olean_header)
            throw exception(sstream() << "file '" << fname << "' does not seem to be a valid object Lean file");
        unsigned major, minor;
        // Perhaps we should enforce the right version number
        d >> major >> minor;
        try {
            if (mod_name)
                add_neutral_object(new import_command(*mod_name));
            while (true) {
                std::string k;
                d >> k;
                if (k == g_olean_end_file) {
                    if (mod_name)
                        add_neutral_object(new end_import_mark());
                    return true;
                }
                read_object(env(), ios, k, d);
            }
        } catch (...) {
            if (mod_name)
                add_neutral_object(new end_import_mark());
            throw;
        }
    } else {
        return false;
    }
}

bool environment_cell::import(std::string const & fname, io_state const & ios) {
    flet<bool> set(m_type_check, !m_trust_imported);
    return load_core(realpath(find_file(fname, {".olean"}).c_str()), ios, optional<std::string>(fname));
}

void environment_cell::load(std::string const & fname, io_state const & ios) {
    load_core(fname, ios, optional<std::string>());
}

bool environment_cell::imported(std::string const & n) const {
    return already_imported(name(realpath(find_file(n, {".olean"}).c_str())));
}

environment_cell::environment_cell():
    m_num_children(0) {
    m_trust_imported = false;
    m_type_check     = true;
    init_uvars();
}

environment_cell::environment_cell(std::shared_ptr<environment_cell> const & parent):
    m_num_children(0),
    m_parent(parent) {
    m_trust_imported = false;
    m_type_check     = true;
    parent->inc_children();
}

environment_cell::~environment_cell() {
    if (m_parent)
        m_parent->dec_children();
}

environment::environment():
    m_ptr(std::make_shared<environment_cell>()) {
    m_ptr->m_this = m_ptr;
    m_ptr->m_type_checker.reset(new type_checker(*this));
}

// used when creating a new child environment
environment::environment(std::shared_ptr<environment_cell> const & parent, bool):
    m_ptr(std::make_shared<environment_cell>(parent)) {
    m_ptr->m_this = m_ptr;
    m_ptr->m_type_checker.reset(new type_checker(*this));
}

// used when creating a reference to the parent environment
environment::environment(std::shared_ptr<environment_cell> const & ptr):
    m_ptr(ptr) {
}

ro_environment::ro_environment(environment const & env):
    m_ptr(env.m_ptr) {
}

ro_environment::ro_environment(weak_ref const & r) {
    if (r.expired())
        throw exception("weak reference to environment object has expired (i.e., the environment has been deleted)");
    m_ptr = r.lock();
}

environment_extension::environment_extension():
    m_env(nullptr),
    m_extid(0) {
}

environment_extension::~environment_extension() {
}

environment_extension const * environment_extension::get_parent_core() const {
    if (m_env == nullptr)
        return nullptr;
    environment_cell * parent = m_env->m_parent.get();
    while (parent) {
        if (m_extid < parent->m_extensions.size()) {
            environment_extension * ext = parent->m_extensions[m_extid].get();
            if (ext)
                return ext;
        }
        parent = parent->m_parent.get();
    }
    return nullptr;
}

read_only_shared_environment::read_only_shared_environment(ro_environment const & env):
    m_env(env),
    m_lock(const_cast<environment_cell*>(m_env.m_ptr.get())->m_mutex) {
}
read_only_shared_environment::~read_only_shared_environment() {}

read_write_shared_environment::read_write_shared_environment(environment const & env):
    m_env(env),
    m_lock(m_env.m_ptr->m_mutex) {
}
read_write_shared_environment::~read_write_shared_environment() {}

static std::unique_ptr<name_map<std::pair<mk_builtin_fn, bool>>> g_available_builtins;
name_map<std::pair<mk_builtin_fn, bool>> & get_available_builtins() {
    if (!g_available_builtins)
        g_available_builtins.reset(new name_map<std::pair<mk_builtin_fn, bool>>());
    return *g_available_builtins;
}

void register_builtin(name const & n, mk_builtin_fn mk, bool is_builtin_set) {
    auto & bs = get_available_builtins();
    if (bs.find(n) != bs.end())
        throw exception("invalid builtin object, system already has a builtin object with the given name"); // LCOV_EXCL_LINE
    bs[n] = mk_pair(mk, is_builtin_set);
}

optional<std::pair<expr, bool>> get_builtin(name const & n) {
    auto it = get_available_builtins().find(n);
    if (it != get_available_builtins().end())
        return optional<std::pair<expr, bool>>(it->second.first(), it->second.second);
    else
        return optional<std::pair<expr, bool>>();
}
}
