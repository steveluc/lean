/*
Copyright (c) 2013 Microsoft Corporation. All rights reserved.
Released under Apache 2.0 license as described in the file LICENSE.

Author: Leonardo de Moura
*/
#pragma once
#include <string>
#include "util/exception.h"

namespace lean {
/**
   \brief Exception for wrapping errors produced by the Lua engine.
*/
class script_exception : public exception {
public:
    enum class source { String, File, Unknown };
private:
    source      m_source;
    std::string m_file;   // if source == File, then this field contains the filename that triggered the error.
    unsigned    m_line;   // line number
    script_exception(source s, std::string f, unsigned l):m_source(s), m_file(f), m_line(l) {}
public:
    script_exception(char const * lua_error);
    virtual ~script_exception();
    virtual char const * what() const noexcept;
    virtual source get_source() const { return m_source; }
    virtual char const * get_filename() const;
    virtual unsigned get_line() const;
    /** \brief Return error message without position information */
    virtual char const * get_msg() const noexcept;
    virtual exception * clone() const { return new script_exception(m_source, m_file, m_line); }
    virtual void rethrow() const { throw *this; }
};
}