// LuauScript, its instances and the script language.
#pragma once

#include <godot_cpp/classes/resource_format_loader.hpp>
#include <godot_cpp/classes/script_extension.hpp>
#include <godot_cpp/classes/script_language_extension.hpp>
#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/templates/hash_set.hpp>
#include <godot_cpp/templates/vector.hpp>
#include <godot_cpp/variant/typed_array.hpp>
#include <lua.h>

#include <vector>

namespace luau {

using namespace godot;

// The self table of an object with a Luau script, pushed instead of the
// object; false if the object has none
bool push_self_table(lua_State *L, GDExtensionObjectPtr object);
// The object of a self table, or null
GDExtensionObjectPtr self_table_owner(lua_State *L, int index);

class LuauLanguage;
struct Member;

class LuauScript : public ScriptExtension {
	GDCLASS(LuauScript, ScriptExtension);

public:
	String source;
	uint64_t source_mtime = 0;     // of the file `source` was read from
	bool valid = false;
	StringName base_type = "RefCounted";
	Ref<LuauScript> base_script;   // `extends = require("res://…")`
	StringName global_name;        // `class_name`
	bool tool = false;
	String icon_path;
	int class_ref = LUA_NOREF;     // the table the script returned

	// ---- Declarations, read once when the script loads (docs/adr/0032)
	struct PropertyDef {
		StringName name;
		Variant::Type type = Variant::NIL;  // NIL: any
		StringName class_name;              // for object types
		PropertyHint hint = PROPERTY_HINT_NONE;
		String hint_string;
		Variant default_value;
		bool has_default = false;
		bool exported = false;              // shown in the inspector and saved
		StringName getter, setter;          // script methods; empty: a plain field
		bool has_accessors() const { return !getter.is_empty() || !setter.is_empty(); }
	};
	struct SignalDef {
		StringName name;
		Vector<StringName> args;
		Vector<Variant::Type> arg_types;
	};
	struct MethodDef {
		StringName name;
		int ref = LUA_NOREF;
		int nparams = 0;  // without self
		bool vararg = false;
		Array defaults;   // for the rightmost parameters
		int defaults_ref = LUA_NOREF;  // the same, as a Lua sequence
		Variant::Type ret_type = Variant::NIL;
		bool has_ret_type = false;
		bool is_static = false;  // `static = { … }`: called without self
		Vector<Variant::Type> arg_types;
	};
	Vector<PropertyDef> properties;  // inspector order
	HashMap<StringName, int> property_index;
	Vector<SignalDef> signals;
	HashMap<StringName, int> signal_index;
	HashMap<StringName, MethodDef> methods;
	// Methods by the StringName's interned data pointer: no string hashing per
	// call (docs/adr/0003)
	HashMap<const void *, const MethodDef *> methods_by_ptr;
	Dictionary constants;
	// Which Object overrides the script defines
	// The overrides, if the script defines them (pointers into methods)
	const MethodDef *get_method = nullptr, *set_method = nullptr, *notification_method = nullptr;
	const MethodDef *property_list_method = nullptr, *validate_property_method = nullptr, *to_string_method = nullptr;
	const MethodDef *can_revert_method = nullptr, *get_revert_method = nullptr;
	// Serves static functions and constants on the script object itself
	// (`preload("x.luau").make()`): a script instance on this resource
	bool has_static_instance = false;
	void ensure_static_instance();

	const PropertyDef *find_property(const StringName &name) const {
		const int *index = property_index.getptr(name);
		return index ? &properties[*index] : nullptr;
	}
	const SignalDef *find_signal(const StringName &name) const {
		const int *index = signal_index.getptr(name);
		return index ? &signals[*index] : nullptr;
	}

	// What a name missing from a self table resolves to, per owner class and
	// name atom (docs/adr/0005). Filled on first use, emptied on reload.
	enum RouteKind : uint8_t { ROUTE_UNRESOLVED, ROUTE_SCRIPT, ROUTE_PROPERTY, ROUTE_SIGNAL, ROUTE_ENGINE };
	struct Route {
		RouteKind kind = ROUTE_UNRESOLVED;
		const Member *member = nullptr;        // ROUTE_ENGINE
		const PropertyDef *property = nullptr; // ROUTE_PROPERTY (accessors)
	};
	struct Routes {
		std::vector<Route> by_atom;
	};
	HashMap<const void *, Routes *> routes;  // by ClassInfo
	Routes *routes_for(const void *class_info);

	static const void *name_ptr(const StringName &name) { return *reinterpret_cast<const void *const *>(name._native_ptr()); }
	const MethodDef *find_method(const StringName &name) const {
		const MethodDef *const *method = methods_by_ptr.getptr(name_ptr(name));
		return method ? *method : nullptr;
	}

	// Inspector placeholders of this script (editor, non-tool scripts)
	HashSet<void *> placeholders;
	void update_placeholders();
	TypedArray<Dictionary> property_list() const;

	~LuauScript();

	bool _editor_can_reload_from_file() override { return true; }
	void _placeholder_erased(void *p_placeholder) override { placeholders.erase(p_placeholder); }
	bool _can_instantiate() const override;
	Ref<Script> _get_base_script() const override { return base_script; }
	StringName _get_global_name() const override { return global_name; }
	bool _inherits_script(const Ref<Script> &p_script) const override;
	StringName _get_instance_base_type() const override { return base_type; }
	void *_instance_create(Object *p_for_object) const override;
	void *_placeholder_instance_create(Object *p_for_object) const override;
	bool _instance_has(Object *p_object) const override;
	bool _has_source_code() const override { return true; }
	String _get_source_code() const override { return source; }
	void _set_source_code(const String &p_code) override { source = p_code; }
	Error _reload(bool p_keep_state) override;
	StringName _get_doc_class_name() const override { return global_name; }
	TypedArray<Dictionary> _get_documentation() const override { return {}; }
	String _get_class_icon_path() const override { return icon_path; }
	bool _has_method(const StringName &p_method) const override { return find_method(p_method) != nullptr; }
	bool _has_static_method(const StringName &p_method) const override {
		const MethodDef *method = find_method(p_method);
		return method != nullptr && method->is_static;
	}
	Variant _get_script_method_argument_count(const StringName &p_method) const override;
	Dictionary _get_method_info(const StringName &p_method) const override;
	bool _is_tool() const override { return tool; }
	bool _is_valid() const override { return valid; }
	bool _is_abstract() const override { return false; }
	ScriptLanguage *_get_language() const override;
	bool _has_script_signal(const StringName &p_signal) const override { return find_signal(p_signal) != nullptr; }
	TypedArray<Dictionary> _get_script_signal_list() const override;
	bool _has_property_default_value(const StringName &p_property) const override;
	Variant _get_property_default_value(const StringName &p_property) const override;
	void _update_exports() override { update_placeholders(); }
	TypedArray<Dictionary> _get_script_method_list() const override;
	TypedArray<Dictionary> _get_script_property_list() const override { return property_list(); }
	int32_t _get_member_line(const StringName &) const override { return -1; }
	Dictionary _get_constants() const override { return constants; }
	TypedArray<StringName> _get_members() const override;
	bool _is_placeholder_fallback_enabled() const override { return false; }
	Variant _get_rpc_config() const override { return {}; }

	// script.new(): an instance of the base type with this script
	Variant _new(const Variant **args, GDExtensionInt argc, GDExtensionCallError &error);

protected:
	static void _bind_methods();
};

class LuauLanguage : public ScriptLanguageExtension {
	GDCLASS(LuauLanguage, ScriptLanguageExtension);
	static LuauLanguage *singleton;

public:
	static LuauLanguage *get_singleton() { return singleton; }
	LuauLanguage() { singleton = this; }
	~LuauLanguage() { singleton = nullptr; }

	String _get_name() const override { return "Luau"; }
	void _init() override;
	String _get_type() const override { return "LuauScript"; }
	String _get_extension() const override { return "luau"; }
	void _finish() override;
	PackedStringArray _get_reserved_words() const override;
	bool _is_control_flow_keyword(const String &) const override { return false; }
	PackedStringArray _get_comment_delimiters() const override { return PackedStringArray({ "--" }); }
	PackedStringArray _get_doc_comment_delimiters() const override { return {}; }
	PackedStringArray _get_string_delimiters() const override { return PackedStringArray({ "\" \"", "' '" }); }
	Ref<Script> _make_template(const String &, const String &, const String &) const override;
	TypedArray<Dictionary> _get_built_in_templates(const StringName &) const override { return {}; }
	bool _is_using_templates() override { return false; }
	Dictionary _validate(const String &, const String &, bool, bool, bool, bool) const override;
	String _validate_path(const String &) const override { return {}; }
	Object *_create_script() const override;
	bool _has_named_classes() const override { return false; }
	bool _supports_builtin_mode() const override { return false; }
	bool _supports_documentation() const override { return false; }
	bool _can_inherit_from_file() const override { return false; }
	int32_t _find_function(const String &, const String &) const override { return -1; }
	String _make_function(const String &, const String &, const PackedStringArray &) const override { return {}; }
	bool _can_make_function() const override { return false; }
	Error _open_in_external_editor(const Ref<Script> &, int32_t, int32_t) override { return ERR_UNAVAILABLE; }
	bool _overrides_external_editor() override { return false; }
	ScriptLanguage::ScriptNameCasing _preferred_file_name_casing() const override { return ScriptLanguage::SCRIPT_NAME_CASING_SNAKE_CASE; }
	Dictionary _complete_code(const String &, const String &, Object *) const override { return {}; }
	Dictionary _lookup_code(const String &, const String &, const String &, Object *) const override { return {}; }
	String _auto_indent_code(const String &p_code, int32_t, int32_t) const override { return p_code; }
	void _add_global_constant(const StringName &, const Variant &) override {}
	void _add_named_global_constant(const StringName &, const Variant &) override {}
	void _remove_named_global_constant(const StringName &) override {}
	void _thread_enter() override {}
	void _thread_exit() override {}
	String _debug_get_error() const override { return {}; }
	int32_t _debug_get_stack_level_count() const override { return 0; }
	int32_t _debug_get_stack_level_line(int32_t) const override { return 0; }
	String _debug_get_stack_level_function(int32_t) const override { return {}; }
	String _debug_get_stack_level_source(int32_t) const override { return {}; }
	Dictionary _debug_get_stack_level_locals(int32_t, int32_t, int32_t) override { return {}; }
	Dictionary _debug_get_stack_level_members(int32_t, int32_t, int32_t) override { return {}; }
	void *_debug_get_stack_level_instance(int32_t) override { return nullptr; }
	Dictionary _debug_get_globals(int32_t, int32_t) override { return {}; }
	String _debug_parse_stack_level_expression(int32_t, const String &, int32_t, int32_t) override { return {}; }
	TypedArray<Dictionary> _debug_get_current_stack_info() override { return {}; }
	void _reload_all_scripts() override;
	void _reload_scripts(const Array &p_scripts, bool p_soft_reload) override;
	void _reload_tool_script(const Ref<Script> &p_script, bool p_soft_reload) override;
	PackedStringArray _get_recognized_extensions() const override { return PackedStringArray({ "luau", "fnl" }); }
	TypedArray<Dictionary> _get_public_functions() const override { return {}; }
	Dictionary _get_public_constants() const override { return {}; }
	TypedArray<Dictionary> _get_public_annotations() const override { return {}; }
	void _profiling_start() override {}
	void _profiling_stop() override {}
	void _profiling_set_save_native_calls(bool) override {}
	void _frame() override;
	bool _handles_global_class_type(const String &p_type) const override { return p_type == "LuauScript"; }
	Dictionary _get_global_class_name(const String &p_path) const override;

protected:
	static void _bind_methods() {}
};

// What a call from Godot that suspended in await returns: Signal(this,
// "completed"), emitted with the call's result (docs/adr/0038). A class with
// a declared signal: cheaper to make than an Object with a user signal.
class LuauCompletion : public Object {
	GDCLASS(LuauCompletion, Object);

protected:
	static void _bind_methods() {
		ADD_SIGNAL(::godot::MethodInfo("completed", ::godot::PropertyInfo(Variant::NIL, "result", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NIL_IS_VARIANT)));
	}
};

class LuauLoader : public ResourceFormatLoader {
	GDCLASS(LuauLoader, ResourceFormatLoader);

public:
	PackedStringArray _get_recognized_extensions() const override { return PackedStringArray({ "luau", "fnl" }); }
	bool _handles_type(const StringName &p_type) const override { return p_type == StringName("Script") || p_type == StringName("LuauScript"); }
	String _get_resource_type(const String &p_path) const override {
		String extension = p_path.get_extension();
		return extension == "luau" || extension == "fnl" ? "LuauScript" : "";
	}
	Variant _load(const String &p_path, const String &p_original_path, bool p_use_sub_threads, int32_t p_cache_mode) const override;

protected:
	static void _bind_methods() {}
};

} // namespace luau
