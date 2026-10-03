// LuauScript, its instances and the script language.
#pragma once

#include <godot_cpp/classes/resource_format_loader.hpp>
#include <godot_cpp/classes/script_extension.hpp>
#include <godot_cpp/classes/script_language_extension.hpp>
#include <godot_cpp/templates/hash_map.hpp>
#include <lua.h>

namespace luau {

using namespace godot;

// The self table of an object with a Luau script, pushed instead of the
// object; false if the object has none
bool push_self_table(lua_State *L, GDExtensionObjectPtr object);
// The object of a self table, or null
GDExtensionObjectPtr self_table_owner(lua_State *L, int index);

class LuauLanguage;

class LuauScript : public ScriptExtension {
	GDCLASS(LuauScript, ScriptExtension);

public:
	String source;
	bool valid = false;
	StringName base_type = "RefCounted";
	int class_ref = LUA_NOREF;          // the table the script returned
	HashMap<StringName, int> methods;   // name → function ref

	~LuauScript();

	bool _editor_can_reload_from_file() override { return true; }
	void _placeholder_erased(void *) override {}
	bool _can_instantiate() const override { return valid; }
	Ref<Script> _get_base_script() const override { return {}; }
	StringName _get_global_name() const override { return {}; }
	bool _inherits_script(const Ref<Script> &) const override { return false; }
	StringName _get_instance_base_type() const override { return base_type; }
	void *_instance_create(Object *p_for_object) const override;
	void *_placeholder_instance_create(Object *) const override { return nullptr; }
	bool _instance_has(Object *) const override { return false; }
	bool _has_source_code() const override { return true; }
	String _get_source_code() const override { return source; }
	void _set_source_code(const String &p_code) override { source = p_code; }
	Error _reload(bool p_keep_state) override;
	StringName _get_doc_class_name() const override { return {}; }
	TypedArray<Dictionary> _get_documentation() const override { return {}; }
	String _get_class_icon_path() const override { return {}; }
	bool _has_method(const StringName &p_method) const override { return methods.has(p_method); }
	bool _has_static_method(const StringName &) const override { return false; }
	Variant _get_script_method_argument_count(const StringName &) const override { return {}; }
	Dictionary _get_method_info(const StringName &) const override { return {}; }
	bool _is_tool() const override { return false; }
	bool _is_valid() const override { return valid; }
	bool _is_abstract() const override { return false; }
	ScriptLanguage *_get_language() const override;
	bool _has_script_signal(const StringName &) const override { return false; }
	TypedArray<Dictionary> _get_script_signal_list() const override { return {}; }
	bool _has_property_default_value(const StringName &) const override { return false; }
	Variant _get_property_default_value(const StringName &) const override { return {}; }
	void _update_exports() override {}
	TypedArray<Dictionary> _get_script_method_list() const override { return {}; }
	TypedArray<Dictionary> _get_script_property_list() const override { return {}; }
	int32_t _get_member_line(const StringName &) const override { return -1; }
	Dictionary _get_constants() const override { return {}; }
	TypedArray<StringName> _get_members() const override { return {}; }
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
	void _reload_all_scripts() override {}
	void _reload_scripts(const Array &, bool) override {}
	void _reload_tool_script(const Ref<Script> &, bool) override {}
	PackedStringArray _get_recognized_extensions() const override { return PackedStringArray({ "luau", "fnl" }); }
	TypedArray<Dictionary> _get_public_functions() const override { return {}; }
	Dictionary _get_public_constants() const override { return {}; }
	TypedArray<Dictionary> _get_public_annotations() const override { return {}; }
	void _profiling_start() override {}
	void _profiling_stop() override {}
	void _profiling_set_save_native_calls(bool) override {}
	void _frame() override {}
	bool _handles_global_class_type(const String &) const override { return false; }
	Dictionary _get_global_class_name(const String &) const override { return {}; }

protected:
	static void _bind_methods() {}
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
