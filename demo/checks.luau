local Checks = { extends = "Node" }

function Checks:keep(node) self.kept = node end
function Checks:use_kept() return self.kept:get_name() end
function Checks:make_ref() self.ref = RefCounted.new(); return self.ref:get_reference_count() end
function Checks:use_ref() return self.ref:get_reference_count() end
function Checks:frames() return Engine:get_physics_ticks_per_second() end
function Checks:keep_self_of(other) self.other = other end
function Checks:use_other() return self.other.name end

return Checks
