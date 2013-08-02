/*
Copyright (c) 2013 Microsoft Corporation. All rights reserved.
Released under Apache 2.0 license as described in the file LICENSE.

Author: Leonardo de Moura
*/
#include <locale>
#include "environment.h"
#include "exception.h"
#include "test.h"
using namespace lean;

static void tst1() {
    environment env;
    level l1 = env.define_uvar("l1", level());
    level l2 = env.define_uvar("l2", l1 + 10);
    level l3 = env.define_uvar("l3", max(l2, l1 + 3));
    level l4 = env.define_uvar("l4", max(l1 + 8, max(l2 + 2, l3 + 20)));
    env.display_uvars(std::cout);
    lean_assert(env.is_ge(l4 + 10, l3 + 30));
    lean_assert(!env.is_ge(l4 + 9, l3 + 30));
}

static void tst2() {
    environment env;
    level l1 = env.define_uvar("l1", level());
    try {
        level l2 = env.define_uvar("l1", level());
        lean_unreachable();
    }
    catch (exception ex) {
        std::cout << "ok\n";
    }
}

static void tst3() {
    environment env;
    level l1 = env.define_uvar("l1", level());
    level l2 = env.define_uvar("l2", l1 + ((1<<30) + 1024));
    try {
        level l3 = env.define_uvar("l3", l2 + (1<<30));
        lean_unreachable();
    }
    catch (exception ex) {
        std::cout << "ok\n";
    }
}

static void tst4() {
    environment env;
    level l1 = env.define_uvar("l1", level() + 1);
    level l2 = env.define_uvar("l2", level() + 1);
    level l3 = env.define_uvar("l3", max(l1, l2) + 1);
    level l4 = env.define_uvar("l4", l3 + 1);
    level l5 = env.define_uvar("l5", l3 + 1);
    level l6 = env.define_uvar("l6", max(l4, l5) + 1);
    lean_assert(!env.is_ge(l5 + 1, l6));
    lean_assert(env.is_ge(l6, l5));
    lean_assert(env.is_ge(l6, max({l1, l2, l3, l4, l5})));
    lean_assert(env.is_ge(l6, l6));
    lean_assert(!env.is_ge(l5, l4));
    lean_assert(env.is_ge(max({l1, l2, l4, l5}), max({l1, l2, l3, l4, l5})));
    lean_assert(env.is_ge(max({l4, l5}), max({l1, l2, l3})));
    lean_assert(!env.is_ge(max({l1, l2, l5}), max({l1, l2, l3, l4, l5})));
    lean_assert(!env.is_ge(max({l2, l4}), max({l1, l2, l3, l4, l5})));
    lean_assert(env.is_ge(max(l2, l3) + 1, max(l1, l1+1)));
    lean_assert(env.is_ge(max(l2, l3) + 1, max(l1+2, l1+1)));
    lean_assert(env.is_ge(max(l2, l3) + 1, max(l2+2, l1+1)));
    lean_assert(!env.is_ge(max(l4, l5) + 1, max(l2+4, l1+1)));
    lean_assert(!env.is_ge(max(l6, l5), max(l2+4, l1+1)));
    lean_assert(env.is_ge(max(l6, l5), max(l2+3, l1+1)));
    lean_assert(!env.is_ge(max(l6, l5), max(l2, l1+1)+3));
    lean_assert(env.is_ge(max(l6+1, l5), max(l2, l1+1)+3));
    env.display_uvars(std::cout);
}

static void tst5() {
    environment env;
    level l1 = env.define_uvar("l1", level() + 1);
    level l2 = env.define_uvar("l2", level() + 1);
    std::cout << max(l1, l1) << "\n";
    lean_assert(max(l1, l1) == l1);
    lean_assert(max(l1+1, l1+1) == l1+1);
    std::cout << max(l1, l1+1) << "\n";
    std::cout << max(l2, max(l1, l1+1)) << "\n";
    lean_assert(max(l1, l1+1) == l1+1);
    lean_assert(max(l2, max(l1, l1+1)) == max(l2, l1+1));
    std::cout << max(l1, max(l2, l1+1)) << "\n";
    lean_assert(max(l1, max(l2, l1+1)) == max(l1+1, l2));
}

int main() {
    continue_on_violation(true);
    tst1();
    tst2();
    tst3();
    tst4();
    tst5();
    return has_violations() ? 1 : 0;
}