# 0051. Exports in inspector categories, groups and subgroups

- **Status:** Accepted
- **Date:** 2026-10-06

## Context

GDScript's `@export_category`, `@export_group` and `@export_subgroup` put
exported properties under headers in the inspector. Godot builds them from
property list entries with `PROPERTY_USAGE_CATEGORY`, `_GROUP` or `_SUBGROUP`,
which apply to the properties after them. godot-luau's `exports`
([0032](0032-script-declarations.md)) had no way to say this, and a map of
exports has no order of its own (it's sorted by name).

## Decision

- **Per property:** `category`, `group` and `subgroup` fields:
  `speed = { default = 1.5, group = "Movement" }`. In a map, exports are
  sorted so each section stays together, unsectioned ones first, then by
  name.
- **In a sequence:** header entries without a name apply to the entries
  after them, as GDScript's annotations do: `{ category = "Stats" }`,
  `{ group = "Movement" }`, `{ subgroup = "Jumping" }`, and `{ group = "" }`
  to end a group. A category starts with no group, and a group with no
  subgroup.
- **Where:** each reload computes `listed`, the properties with a header
  entry wherever an export's category, group or subgroup changes. The script's
  property list, instances' lists and editor placeholders all use it;
  `properties` (looked up by name and index) is unchanged.

## Consequences

- `demo/checks.gd` checks the property lists of `features/sections_list.luau`
  (headers in a sequence, ending a group) and `features/sections_map.luau`
  (per-property groups in a map).
- Groups' prefix (`@export_group("Name", "prefix")`, which strips a name
  prefix in the inspector) isn't supported.
