local Checks = { extends = "Node" }

function Checks:keep(node) self.kept = node end
function Checks:use_kept() return self.kept:get_name() end
function Checks:make_ref() self.ref = RefCounted.new(); return self.ref:get_reference_count() end
function Checks:use_ref() return self.ref:get_reference_count() end
function Checks:frames() return Engine:get_physics_ticks_per_second() end
function Checks:keep_self_of(other) self.other = other end
function Checks:use_other() return self.other.name end

function Checks:dict_write(d) d["x"] = 5; d[1] = "y"; return d["x"] end
function Checks:array_write(a) a[0] = 10; return a[0] + a[1] end
function Checks:array_oob(a) return a[99] end

function Checks:echo(value) return value end
function Checks:concat(a, b) return a .. b end
function Checks:lua_length(s) return #s end

return Checks
