/*
Copyright (c) 2013 Microsoft Corporation. All rights reserved.
Released under Apache 2.0 license as described in the file LICENSE.

Author: Leonardo de Moura
*/
#pragma once
#include <algorithm>
#include <cstring>
#include "debug.h"

namespace lean {

template<typename T, unsigned INITIAL_SIZE=16>
class buffer {
protected:
    T *      m_buffer;
    unsigned m_pos;
    unsigned m_capacity;
    char     m_initial_buffer[INITIAL_SIZE * sizeof(T)];

    void free_memory() {
        if (m_buffer != reinterpret_cast<T*>(m_initial_buffer))
            delete[] reinterpret_cast<char*>(m_buffer);
    }

    void expand() {
        unsigned new_capacity = m_capacity << 1;
        T * new_buffer        = reinterpret_cast<T*>(new char[sizeof(T) * new_capacity]);
        std::memcpy(new_buffer, m_buffer, m_pos * sizeof(T));
        free_memory();
        m_buffer              = new_buffer;
        m_capacity            = new_capacity;
    }

    void destroy_elements() {
        std::for_each(begin(), end(), [](T & e) { e.~T(); });
    }

    void destroy() {
        destroy_elements();
        free_memory();
    }

public:
    typedef T value_type;
    typedef T * iterator;
    typedef T const * const_iterator;

    buffer():
        m_buffer(reinterpret_cast<T *>(m_initial_buffer)),
        m_pos(0),
        m_capacity(INITIAL_SIZE) {
    }

    buffer(buffer const & source):
        m_buffer(reinterpret_cast<T *>(m_initial_buffer)),
        m_pos(0),
        m_capacity(INITIAL_SIZE) {
        std::for_each(source.begin(), source.end(), [=](T const & e) { push_back(e); });
    }

    ~buffer() { destroy(); }

    buffer & operator=(buffer const & source) {
        clear();
        std::for_each(source.begin(), source.end(), [=](T const & e) { push_back(e); });
        return *this;
    }

    T const & back() const { lean_assert(!empty() && m_pos > 0); return m_buffer[m_pos - 1]; }
    T & back() { lean_assert(!empty() && m_pos > 0); return m_buffer[m_pos - 1]; }
    T & operator[](unsigned idx) { lean_assert(idx < size());  return m_buffer[idx]; }
    T const & operator[](unsigned idx) const { lean_assert(idx < size()); return m_buffer[idx]; }
    void clear() { destroy_elements(); m_pos = 0; }
    unsigned size() const { return m_pos; }
    T const * data() const { return m_buffer; }
    T * data() { return m_buffer; }
    bool empty() const { return m_pos == 0;  }
    iterator begin() { return m_buffer; }
    iterator end() { return m_buffer + size(); }
    const_iterator begin() const { return m_buffer; }
    const_iterator end() const { return m_buffer + size(); }

    void push_back(T const & elem) {
        if (m_pos >= m_capacity)
            expand();
        new (m_buffer + m_pos) T(elem);
        m_pos++;
    }

    void pop_back() {
        back().~T();
        m_pos--;
    }

    void append(unsigned sz, T const * elems) {
        for (unsigned i = 0; i < sz; i++)
            push_back(elems[i]);
    }

    void resize(unsigned nsz, T const & elem=T()) {
        unsigned sz = size();
        if (nsz > sz) {
            for (unsigned i = sz; i < nsz; i++)
                push_back(elem);
        }
        else if (nsz < sz) {
            for (unsigned i = nsz; i < sz; i++)
                pop_back();
        }
        lean_assert(size() == nsz);
    }

    void shrink(unsigned nsz) {
        unsigned sz = size();
        lean_assert(nsz <= sz);
        for (unsigned i = nsz; i < sz; i++)
            pop_back();
        lean_assert(size() == nsz);
    }
};

}