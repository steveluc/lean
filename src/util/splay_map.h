/*
Copyright (c) 2013 Microsoft Corporation. All rights reserved.
Released under Apache 2.0 license as described in the file LICENSE.

Author: Leonardo de Moura
*/
#pragma once
#include <utility>
#include "util/lua.h"
#include "util/pair.h"
#include "util/splay_tree.h"

namespace lean {
/**
   \brief Wrapper for implementing maps using splay trees.
*/
template<typename K, typename T, typename CMP>
class splay_map : public CMP {
public:
    typedef std::pair<K, T> entry;
private:
    struct entry_cmp : public CMP {
        entry_cmp(CMP const & c):CMP(c) {}
        int operator()(entry const & e1, entry const & e2) const { return CMP::operator()(e1.first, e2.first); }
    };
    splay_tree<entry, entry_cmp> m_map;
public:
    splay_map(CMP const & cmp = CMP()):m_map(entry_cmp(cmp)) {
        // the return type of CMP()(k1, k2) should be int
        static_assert(std::is_same<typename std::result_of<decltype(std::declval<CMP>())(K const &, K const &)>::type,
                                   int>::value,
                      "The return type of CMP()(k1, k2) is not int.");
    }
    friend void swap(splay_map & a, splay_map & b) { swap(a.m_map, b.m_map); }
    bool empty() const { return m_map.empty(); }
    void clear() { m_map.clear(); }
    bool is_eqp(splay_map const & m) const { return m_map.is_eqp(m); }
    unsigned size() const { return m_map.size(); }
    void insert(K const & k, T const & v) { m_map.insert(mk_pair(k, v)); }
    T const * find(K const & k) const { auto e = m_map.find(mk_pair(k, T())); return e ? &(e->second) : nullptr; }
    T * find(K const & k) { auto e = m_map.find(mk_pair(k, T())); return e ? &(const_cast<T&>(e->second)) : nullptr; }
    bool contains(K const & k) const { return m_map.contains(mk_pair(k, T())); }
    void erase(K const & k) { m_map.erase(mk_pair(k, T())); }

    class ref {
        splay_map & m_map;
        K const &   m_key;
    public:
        ref(splay_map & m, K const & k):m_map(m), m_key(k) {}
        ref & operator=(T const & v) { m_map.insert(m_key, v); return *this; }
        operator T const &() const {
            T const * e = m_map.find(m_key);
            if (e) {
                return *e;
            } else {
                m_map.insert(m_key, T());
                return *(m_map.find(m_key));
            }
        }
    };

    /**
       \brief Returns a reference to the value that is mapped to a key equivalent to key,
       performing an insertion if such key does not already exist.
    */
    ref operator[](K const & k) { return ref(*this, k); }

    template<typename F, typename R>
    R fold(F f, R r) const {
        static_assert(std::is_same<typename std::result_of<F(K const &, T const &, R const &)>::type, R>::value,
                      "fold: return type of f(k : K, t : T, r : R) is not R");
        auto f_prime = [&](entry const & e, R r) -> R { return f(e.first, e.second, r); };
        return m_map.fold(f_prime, r);
    }

    template<typename F>
    void for_each(F f) const {
        static_assert(std::is_same<typename std::result_of<F(K const &, T const &)>::type, void>::value,
                      "for_each: return type of f is not void");
        auto f_prime = [&](entry const & e) { f(e.first, e.second); };
        return m_map.for_each(f_prime);
    }

    /** \brief (For debugging) Display the content of this splay map. */
    friend std::ostream & operator<<(std::ostream & out, splay_map const & m) {
        out << "{";
        m.for_each([&out](K const & k, T const & v) {
                out << k << " |-> " << v << "; ";
            });
        out << "}";
        return out;
    }
};
template<typename K, typename T, typename CMP>
splay_map<K, T, CMP> insert(splay_map<K, T, CMP> const & m, K const & k, T const & v) {
    auto r = m;
    r.insert(k, v);
    return r;
}
template<typename K, typename T, typename CMP>
splay_map<K, T, CMP> erase(splay_map<K, T, CMP> const & m, K const & k) {
    auto r = m;
    r.erase(k);
    return r;
}
template<typename K, typename T, typename CMP, typename F, typename R>
R fold(splay_map<K, T, CMP> const & m, F f, R r) {
    static_assert(std::is_same<typename std::result_of<F(K const &, T const &, R const &)>::type, R>::value,
                  "fold: return type of f(k : K, t : T, r : R) is not R");
    return m.fold(f, r);
}
template<typename K, typename T, typename CMP, typename F>
void for_each(splay_map<K, T, CMP> const & m, F f) {
    static_assert(std::is_same<typename std::result_of<F(K const &, T const &)>::type, void>::value,
                  "for_each: return type of f is not void");
    return m.for_each(f);
}

void open_splay_map(lua_State * L);
}
