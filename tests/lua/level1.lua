l = level()
assert(is_level(l))
assert(l:is_bottom())
assert(l:kind() == level_kind.UVar)
l = level(l, 1)
assert(is_level(l))
assert(not l:is_bottom())
assert(l:is_lift())
assert(l:kind() == level_kind.Lift)
assert(l:lift_of() == level())
assert(l:lift_offset() == 1)
l = level("U")
assert(l:is_uvar())
assert(l:uvar_name() == name("U"))
assert(not l:is_lift())
l = level(level("U"), level("M"), level("m"))
assert(l:is_max())
assert(l:max_size() == 3)
assert(l:max_level(0) == level("U"))
assert(l:max_level(1) == level("M"))
print(l)
assert(l:kind() == level_kind.Max)
