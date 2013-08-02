/*
Copyright (c) 2013 Microsoft Corporation. All rights reserved.
Released under Apache 2.0 license as described in the file LICENSE.

Author: Leonardo de Moura
*/
#include <algorithm>
#include "level.h"
#include "buffer.h"
#include "rc.h"
#include "debug.h"
#include "hash.h"

namespace lean {
struct level_cell {
    void dealloc();
    MK_LEAN_RC()
    level_kind m_kind;
    level_cell(level_kind k):m_rc(1), m_kind(k) {}
};
struct level_uvar : public level_cell {
    name m_name;
    uvar m_uvar;
    level_uvar(name const & n, uvar u):level_cell(level_kind::UVar), m_name(n), m_uvar(u) {}
};
struct level_lift : public level_cell {
    level    m_l;
    unsigned m_k;
    level_lift(level const & l, unsigned k):level_cell(level_kind::Lift), m_l(l), m_k(k) {}
};
struct level_max : public level_cell {
    unsigned m_size;
    level    m_levels[0];
    level_max(unsigned sz, level const * ls):level_cell(level_kind::Max), m_size(sz) {
        for (unsigned i = 0; i < m_size; i++)
            new (m_levels + i) level(ls[i]);
    }
    ~level_max() {
        for (unsigned i = 0; i < m_size; i++)
            (m_levels+i)->~level();
    }
    level const * begin_levels() const { return m_levels; }
    level const * end_levels() const { return m_levels + m_size; }
};
level_uvar * to_uvar(level_cell * c) { return static_cast<level_uvar*>(c); }
level_lift * to_lift(level_cell * c) { return static_cast<level_lift*>(c); }
level_max  * to_max(level_cell * c)  { return static_cast<level_max*>(c); }
void level_cell::dealloc() {
    switch (m_kind) {
    case level_kind::UVar:    delete to_uvar(this); break;
    case level_kind::Lift:    delete to_lift(this); break;
    case level_kind::Max:     static_cast<level_max*>(this)->~level_max(); delete[] reinterpret_cast<char*>(this); break;
    }
}
level::level():                           m_ptr(new level_uvar(name("bot"), 0)) { lean_assert(uvar_name(*this) == name("bot")); }
level::level(name const & n, uvar u):     m_ptr(new level_uvar(n, u)) {}
level::level(level const & l, unsigned k):m_ptr(new level_lift(l, k)) { lean_assert(is_uvar(l)); }
level::level(level_cell * ptr):           m_ptr(ptr) { lean_assert(m_ptr->get_rc() == 1); }
level::level(level const & s):
    m_ptr(s.m_ptr) {
    if (m_ptr)
        m_ptr->inc_ref();
}
level::level(level && s):
    m_ptr(s.m_ptr) {
    s.m_ptr = nullptr;
}
level::~level() {
    if (m_ptr)
        m_ptr->dec_ref();
}
level to_level(level_cell * c) { return level(c); }
level_cell * to_cell(level const & l) { return l.m_ptr; }
level_max * to_max(level const & l) { return to_max(to_cell(l)); }
unsigned level::hash() const {
    switch (m_ptr->m_kind) {
    case level_kind::UVar: return to_uvar(m_ptr)->m_name.hash();
    case level_kind::Lift: return ::lean::hash(to_lift(m_ptr)->m_l.hash(), to_lift(m_ptr)->m_k);
    case level_kind::Max:  return ::lean::hash(to_max(m_ptr)->m_size, [&](unsigned i) { return to_max(m_ptr)->m_levels[i].hash(); });
    }
    return 0;
}

level max_core(unsigned sz, level const * ls) {
    char * mem   = new char[sizeof(level_max) + sz*sizeof(level)];
    return to_level(new (mem) level_max(sz, ls));
}

level const & _lift_of(level const & l)     { return is_lift(l) ? lift_of(l) : l; }
unsigned      _lift_offset(level const & l) { return is_lift(l) ? lift_offset(l) : 0; }

void push_back(buffer<level> & ls, level const & l) {
    for (unsigned i = 0; i < ls.size(); i++) {
        if (_lift_of(ls[i]) == _lift_of(l)) {
            if (_lift_offset(ls[i]) < _lift_offset(l))
                ls[i] = l;
            return;
        }
    }
    ls.push_back(l);
}

level max_core(unsigned sz1, level const * ls1, unsigned sz2, level const * ls2) {
    buffer<level> new_lvls;
    std::for_each(ls1, ls1 + sz1, [&](level const & l) { push_back(new_lvls, l); });
    std::for_each(ls2, ls2 + sz2, [&](level const & l) { push_back(new_lvls, l); });
    if (new_lvls.size() == 1)
        return new_lvls[0];
    else
        return max_core(new_lvls.size(), new_lvls.data());
}

level max_core(level const & l1, level const & l2) { return max_core(1, &l1, 1, &l2); }
level max_core(level const & l1, level_max * l2) { return max_core(1, &l1, l2->m_size, l2->m_levels); }
level max_core(level_max * l1, level const & l2) { return max_core(l1->m_size, l1->m_levels, 1, &l2); }
level max_core(level_max * l1, level_max * l2) { return max_core(l1->m_size, l1->m_levels, l2->m_size, l2->m_levels); }

level max(level const & l1, level const & l2) {
    if (is_max(l1)) {
        if (is_max(l2))
            return max_core(to_max(l1), to_max(l1));
        else
            return max_core(to_max(l1), l2);
    }
    else {
        if (is_max(l2))
            return max_core(l1, to_max(l2));
        else
            return max_core(l1, l2);
    }
}

level operator+(level const & l, unsigned k)  {
    switch (kind(l)) {
    case level_kind::UVar: return level(l, k);
    case level_kind::Lift: return level(lift_of(l), lift_offset(l) + k);
    case level_kind::Max: {
        std::cout << l << "\n";
        buffer<level> new_lvls;
        for (unsigned i = 0; i < max_size(l); i++)
            new_lvls.push_back(max_level(l, i) + k);
        return max_core(new_lvls.size(), new_lvls.data());
    }}
    lean_unreachable();
    return level();
}

level_kind    kind       (level const & l) { lean_assert(l.m_ptr); return l.m_ptr->m_kind; }
name const &  uvar_name  (level const & l) { lean_assert(is_uvar(l)); return static_cast<level_uvar*>(l.m_ptr)->m_name; }
uvar          uvar_idx   (level const & l) { lean_assert(is_uvar(l)); return static_cast<level_uvar*>(l.m_ptr)->m_uvar; }
level const & lift_of    (level const & l) { lean_assert(is_lift(l)); return static_cast<level_lift*>(l.m_ptr)->m_l; }
unsigned      lift_offset(level const & l) { lean_assert(is_lift(l)); return static_cast<level_lift*>(l.m_ptr)->m_k; }
unsigned      max_size   (level const & l) { lean_assert(is_max(l));  return static_cast<level_max*>(l.m_ptr)->m_size; }
level const & max_level  (level const & l, unsigned i) { lean_assert(is_max(l));  return static_cast<level_max*>(l.m_ptr)->m_levels[i]; }

level & level::operator=(level const & l) { LEAN_COPY_REF(level, l); }
level & level::operator=(level&& l) { LEAN_MOVE_REF(level, l); }

bool operator==(level const & l1, level const & l2) {
    if (kind(l1) != kind(l2)) return false;
    switch (kind(l1)) {
    case level_kind::UVar: return uvar_idx(l1) == uvar_idx(l2);
    case level_kind::Lift: return lift_of(l1)  == lift_of(l2)  && lift_offset(l1) == lift_offset(l2);
    case level_kind::Max:
        if (max_size(l1) == max_size(l2)) {
            for (unsigned i = 0; i < max_size(l1); i++)
                if (max_level(l1, i) != max_level(l2, i))
                    return false;
            return true;
        } else {
            return false;
        }
    }
    lean_unreachable();
    return false;
}

std::ostream & operator<<(std::ostream & out, level const & l) {
    switch (kind(l)) {
    case level_kind::UVar: out << uvar_name(l);                        return out;
    case level_kind::Lift: out << lift_of(l) << "+" << lift_offset(l); return out;
    case level_kind::Max:
        out << "(max";
        for (unsigned i = 0; i < max_size(l); i++)
            out << " " << max_level(l, i);
        out << ")";
        return out;
    }
    return out;
}

level max(std::initializer_list<level> const & l) {
    if (l.size() == 1)
        return *(l.begin());
    return max_core(l.size(), l.begin());
}
}