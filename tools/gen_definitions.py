"""Generates a Luau definitions file for Godot's API from extension_api.json,
for luau-lsp: completion, hover and type checking in external editors
(docs/adr/0048).

    python3 tools/gen_definitions.py extension_api.json godot.d.luau [gd_docs.fnl]

With a third path it also writes fennel-ls's hover and completion entries
for gd (docs/adr/0061): a Fennel table of signatures, which the addon's
lsp.fnlm passes as :global-docs.

Types follow what godot-luau converts: numbers for int and float, strings for
String and StringName, Lua tables where Arrays and Dictionaries are taken,
functions where Callables are taken. Engine classes are external types with
their properties, signals and methods; class tables have `new` and their
constants; built-in types have their members, methods, operators,
constructors, constants and static methods.
"""
import json
import re
import sys

KEYWORDS = {
    "and", "break", "do", "else", "elseif", "end", "false", "for", "function", "if", "in",
    "local", "nil", "not", "or", "repeat", "return", "then", "true", "until", "while",
}
# Built-in types that are Lua values rather than declared types
NOT_DECLARED = {"Nil", "bool", "int", "float", "String", "StringName", "Object", "Variant"}
OPERATORS = {"+": "__add", "-": "__sub", "*": "__mul", "/": "__div", "%": "__mod", "<": "__lt", "<=": "__le"}
PACKED_ELEMENTS = {
    "PackedByteArray": "number", "PackedInt32Array": "number", "PackedInt64Array": "number",
    "PackedFloat32Array": "number", "PackedFloat64Array": "number", "PackedStringArray": "string",
    "PackedVector2Array": "Vector2", "PackedVector3Array": "Vector3", "PackedVector4Array": "Vector4",
    "PackedColorArray": "Color",
}
# Arguments of these types also take the other one: Godot converts them
CONVERTIBLE = {
    "Vector2": "Vector2i", "Vector2i": "Vector2", "Vector3": "Vector3i", "Vector3i": "Vector3",
    "Vector4": "Vector4i", "Vector4i": "Vector4", "Rect2": "Rect2i", "Rect2i": "Rect2",
}
IDENTIFIER = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")


class Api:
    def __init__(self, api):
        self.classes = {c["name"]: c for c in api["classes"]}
        self.builtins = {b["name"]: b for b in api["builtin_classes"]}
        self.singletons = {s["name"] for s in api["singletons"]}

    def type(self, godot, argument=False):
        """The Luau type for a Godot type name, as an argument or a value."""
        if not godot:
            return "()"
        if godot in ("int", "float"):
            return "number"
        if godot == "bool":
            return "boolean"
        if godot in ("String", "StringName"):
            return "string"
        if godot == "Variant":
            return "any"
        if godot.startswith(("enum::", "bitfield::")):
            return "number"
        if godot.startswith("typedarray::"):
            element = self.type(godot[len("typedarray::"):])
            return f"Array | {{{element}}}" if argument else "Array"
        if godot.startswith("typeddictionary::"):
            return "Dictionary | {[any]: any}" if argument else "Dictionary"
        if argument:
            if godot == "NodePath":
                return "NodePath | string"
            if godot == "Array":
                return "Array | {any}"
            if godot == "Dictionary":
                return "Dictionary | {[any]: any}"
            if godot == "Callable":
                return "Callable | (...any) -> ...any"
            if godot in PACKED_ELEMENTS:
                return f"{godot} | {{{PACKED_ELEMENTS[godot]}}}"
            if godot in CONVERTIBLE:
                return f"{godot} | {CONVERTIBLE[godot]}"
        if godot in self.builtins and godot not in NOT_DECLARED:
            return godot
        if godot == "Object" or godot in self.classes:
            return f"{godot}?" if argument else godot
        return "any"  # pointers and native structures

    def parameters(self, arguments, vararg, declaration=False):
        """Parameters, for a function type or (declaration) a declared function."""
        out = []
        for a in arguments or []:
            name = a["name"] + "_" if a["name"] in KEYWORDS else a["name"]
            t = self.type(a["type"], argument=True)
            if "default_value" in a:
                t = f"({t})?" if "|" in t or "->" in t else (t if t.endswith("?") else t + "?")
            out.append(f"{name}: {t}")
        if vararg:
            out.append("...: any" if declaration else "...any")
        return out

    def returns(self, method):
        if "return_value" in method:  # classes
            return self.type(method["return_value"]["type"])
        return self.type(method.get("return_type", ""))  # built-in types


def method_line(api, owner, method):
    """A method of an external type: `function name(self, ...): R`, or a
    property holding a function when the name is a Luau keyword."""
    result = api.returns(method)
    if method["name"] in KEYWORDS:
        args = ", ".join([owner] + api.parameters(method.get("arguments"), method.get("is_vararg")))
        return f'\t["{method["name"]}"]: ({args}) -> {result}'
    params = api.parameters(method.get("arguments"), method.get("is_vararg"), declaration=True)
    return f"\tfunction {method['name']}({', '.join(['self'] + params)}): {result}"


def field(name, t):
    return f"{name}: {t}" if IDENTIFIER.match(name) and name not in KEYWORDS else f'["{name}"]: {t}'


def builtin_type(api, b, out, gd):
    name = b["name"]
    seen = set()
    body = []
    for m in b.get("members", []):
        seen.add(m["name"])
        body.append("\t" + field(m["name"], api.type(m["type"])))
    if b.get("indexing_return_type"):
        body.append(f"\t[number]: {api.type(b['indexing_return_type'])}")
    overloads = {}
    for o in b.get("operators", []):
        event = OPERATORS.get(o["name"])
        if not event or "right_type" not in o or o["right_type"] == "Variant":
            continue
        overloads.setdefault(event, []).append(f"({name}, {api.type(o['right_type'])}) -> {api.type(o['return_type'])}")
    for o in b.get("operators", []):
        if o["name"] == "unary-":
            overloads.setdefault("__unm", []).append(f"({name}) -> {api.type(o['return_type'])}")
    for event, types in overloads.items():
        body.append(f"\t{event}: " + " & ".join(f"({t})" for t in types))
    for m in b.get("methods", []):
        if not m.get("is_static") and m["name"] not in seen:
            seen.add(m["name"])
            body.append(method_line(api, name, m))
    out.append(f"declare extern type {name} with")
    out.extend(body)
    out.append("end")

    # gd's member: constructors, constants, enum values and static methods
    constructors = [f"({', '.join(api.parameters(c.get('arguments'), False))}) -> {name}" for c in b.get("constructors", [])]
    members = []
    for c in b.get("constants", []):
        members.append(field(c["name"], api.type(c["type"])))
    for e in b.get("enums", []):
        for v in e["values"]:
            members.append(field(v["name"], "number"))
    for m in b.get("methods", []):
        params = api.parameters(m.get("arguments"), m.get("is_vararg"))
        if not m.get("is_static"):
            params = [f"self: {name}"] + params  # Vector2.dot(a, b)
        members.append(field(m["name"], f"({', '.join(params)}) -> {api.returns(m)}"))
    table = "{ " + ", ".join(members) + " }" if members else "{}"
    callable_part = " & ".join(f"({c})" for c in constructors) if constructors else ""
    out.append("")
    gd.append(field(name, f"{callable_part + ' & ' if callable_part else ''}{table}"))


def engine_class(api, c, out):
    name = c["name"]
    seen = set()
    body = []
    for p in c.get("properties", []):
        if IDENTIFIER.match(p["name"]) and p["name"] not in seen:
            seen.add(p["name"])
            t = api.type(p["type"].split(",")[0])
            body.append("\t" + field(p["name"], t))
    for s in c.get("signals", []):
        if s["name"] not in seen:
            seen.add(s["name"])
            body.append("\t" + field(s["name"], "Signal"))
    for m in c.get("methods", []):
        if m.get("is_virtual") or m.get("is_static") or m["name"] in seen:
            continue
        seen.add(m["name"])
        body.append(method_line(api, name, m))
    parent = f" extends {c['inherits']}" if c.get("inherits") else ""
    out.append(f"declare extern type {name}{parent} with")
    out.extend(body)
    out.append("end")


def class_table(api, c, gd):
    """gd's member for an engine class: `new` and constants, or the singleton."""
    name = c["name"]
    if name in api.singletons:
        gd.append(field(name, name))
        return
    members = []
    if c.get("is_instantiable"):
        members.append(f"new: () -> {name}")
    for k in c.get("constants", []):
        members.append(field(k["name"], "number"))
    for e in c.get("enums", []):
        for v in e["values"]:
            members.append(field(v["name"], "number"))
    gd.append(field(name, f"{{ {', '.join(members)} }}"))


def ordered_classes(api):
    """Parents before children."""
    done, order = set(), []

    def visit(name):
        if name in done or name not in api.classes:
            return
        visit(api.classes[name].get("inherits"))
        done.add(name)
        order.append(api.classes[name])

    for name in sorted(api.classes):
        visit(name)
    return order


def fennel_string(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n") + '"'


def fennel_docs(api, data):
    """fennel-ls :global-docs entries for gd: {"gd.X" {:signature "…" :doc "…"}}.
    extension_api.json has no descriptions, so each is a signature, typed as
    godot-luau converts (Lua numbers, strings and tables)."""
    docs = {}

    def godot_type(t):
        """Godot's name for a type, as its docs write it"""
        if not t:
            return ""
        for prefix in ("enum::", "bitfield::"):
            if t.startswith(prefix):
                return t[len(prefix):]
        if t.startswith("typedarray::"):
            return f"Array[{t[len('typedarray::'):]}]"
        return t

    def returns(m):
        return godot_type(m["return_value"]["type"] if "return_value" in m else m.get("return_type", ""))

    def call(name, arguments, vararg, result):
        params = [f"{a['name']}:{godot_type(a['type'])}" + ("?" if "default_value" in a else "") for a in arguments or []]
        if vararg:
            params.append("...")
        return f"({' '.join([name] + params)})" + (f" -> {result}" if result else "")

    def add(key, signature, doc=None):
        docs.setdefault(key, (signature, doc))

    def ancestry(c):
        chain = []
        while c.get("inherits"):
            chain.append(c["inherits"])
            c = api.classes.get(c["inherits"], {})
        return " < ".join(chain)

    for name, b in sorted(api.builtins.items()):
        if name in ("Nil", "Variant", "Object", "StringName"):
            continue
        key = f"gd.{name}"
        overloads = [call(key, c.get("arguments"), False, name) for c in b.get("constructors", [])]
        add(key, "\n".join(dict.fromkeys(overloads)) or key, f"Godot's built-in type {name}: a type for declarations, and its constructors")
        for c in b.get("constants", []):
            add(f"{key}.{c['name']}", f"{key}.{c['name']} : {godot_type(c['type'])}", f"= {c['value']}")
        for e in b.get("enums", []):
            for v in e["values"]:
                add(f"{key}.{v['name']}", f"{key}.{v['name']} : int", f"{e['name']} = {v['value']}")
        for m in b.get("methods", []):
            if m.get("is_static"):
                add(f"{key}.{m['name']}", call(f"{key}.{m['name']}", m.get("arguments"), m.get("is_vararg"), returns(m)))
    for c in ordered_classes(api):
        name = c["name"]
        key = f"gd.{name}"
        parents = ancestry(c)
        if name in api.singletons:
            add(key, f"{key} : {name}", "The engine singleton" + (f" ({parents})" if parents else ""))
            for m in c.get("methods", []):
                if not m.get("is_virtual") and not m.get("is_static"):
                    add(f"{key}:{m['name']}", call(f"{key}:{m['name']}", m.get("arguments"), m.get("is_vararg"), returns(m)))
        else:
            add(key, key, f"Engine class {name}" + (f" < {parents}" if parents else "") + ": a base for extends, a type for declarations")
            if c.get("is_instantiable"):
                add(f"{key}.new", f"({key}.new) -> {name}")
        for k in c.get("constants", []):
            add(f"{key}.{k['name']}", f"{key}.{k['name']} : int", f"= {k['value']}")
        for e in c.get("enums", []):
            for v in e["values"]:
                add(f"{key}.{v['name']}", f"{key}.{v['name']} : int", f"{e['name']} = {v['value']}")
        for m in c.get("methods", []):
            if m.get("is_static"):
                add(f"{key}.{m['name']}", call(f"{key}.{m['name']}", m.get("arguments"), m.get("is_vararg"), returns(m)))
    for f in data["utility_functions"]:
        add(f"gd.{f['name']}", call(f"gd.{f['name']}", f.get("arguments"), f.get("is_vararg"), godot_type(f.get("return_type", ""))))
    for e in data["global_enums"]:
        for v in e["values"]:
            add(f"gd.{v['name']}", f"gd.{v['name']} : int", f"{e['name']} = {v['value']}")
    add("gd", "gd", "The Godot API: engine classes, built-in types, singletons, constants and utility functions (docs/adr/0056)")
    add("await", "(await signal) -> ...", "Waits for the signal and returns its arguments (docs/adr/0035)")
    add("spawn", "(spawn f ...)", "Runs f in a new coroutine that can await")

    lines = [";; Godot's API for fennel-ls hover and completion: generated by",
             ";; tools/gen_definitions.py from extension_api.json (docs/adr/0061)", "{"]
    for key in sorted(docs):
        signature, doc = docs[key]
        entry = f":signature {fennel_string(signature)}" + (f" :doc {fennel_string(doc)}" if doc else "")
        lines.append(f" {fennel_string(key)} {{{entry}}}")
    lines.append("}")
    return "\n".join(lines) + "\n"


def main(source, target, fennel_target=None):
    data = json.load(open(source))
    api = Api(data)
    h = data["header"]
    out = [
        f"-- Godot {h['version_major']}.{h['version_minor']} API for godot-luau, generated by",
        "-- tools/gen_definitions.py from extension_api.json. For luau-lsp:",
        '--   "luau-lsp.types.definitionFiles": { "@godot": "addons/godot_luau/types/godot.d.luau" }',
        "",
    ]
    gd = []  # the Godot API's values, under one global (docs/adr/0056)
    for name in sorted(api.builtins):
        if name not in NOT_DECLARED:
            builtin_type(api, api.builtins[name], out, gd)
    # Lua's own value types: on gd for declared types (docs/adr/0059) and
    # as Godot's constructors
    for name in ["bool", "int", "float", "String"]:
        b = api.builtins[name]
        constructors = [f"({', '.join(api.parameters(c.get('arguments'), False))}) -> {api.type(name)}" for c in b.get("constructors", [])]
        gd.append(field(name, " & ".join(f"({c})" for c in dict.fromkeys(constructors))))
    classes = ordered_classes(api)
    for c in classes:
        engine_class(api, c, out)
    out.append("")
    for c in classes:
        class_table(api, c, gd)
    for f in data["utility_functions"]:
        params = api.parameters(f.get("arguments"), f.get("is_vararg"))
        gd.append(field(f["name"], f"({', '.join(params)}) -> {api.type(f.get('return_type', ''))}"))
    for e in data["global_enums"]:
        for v in e["values"]:
            gd.append(field(v["name"], "number"))
    out.append("declare extern type Godot with")
    out.extend(f"\t{m}" for m in gd)
    out.append("end")
    out.append("declare gd: Godot")
    out.append("")
    # Godot's String methods on the string library (string.to_upper(s)); method
    # calls on strings (s:to_upper()) use Luau's built-in string type, which a
    # definitions file can't extend
    string_methods = [
        field(m["name"], f"({', '.join(['self: string'] + api.parameters(m.get('arguments'), m.get('is_vararg')))}) -> {api.returns(m)}")
        for m in api.builtins["String"].get("methods", []) if not m.get("is_static")
    ]
    out.append("declare string: typeof(string) & { " + ", ".join(string_methods) + " }")
    out += [
        "",
        "-- godot-luau's own globals",
        "declare extern type int64 with end  -- opaque integers beyond ±2^53 (typeof gives \"int64\")",
        "declare package: { loaded: { [string]: any }, preload: { [string]: (...any) -> any }, path: string }",
        "declare function __luau_thread_stats(): { [string]: number }",
        "declare function await(signal: Signal | Object | any): ...any",
        "declare function spawn(f: (...any) -> ...any, ...: any): ()",
        "",
    ]
    with open(target, "w") as f:
        f.write("\n".join(out))
    if fennel_target:
        with open(fennel_target, "w") as f:
            f.write(fennel_docs(api, data))


if __name__ == "__main__":
    main(*sys.argv[1:4])
