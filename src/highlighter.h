// The script editor's highlighter for Luau and Fennel (docs/adr/0053)
#pragma once

#include <godot_cpp/classes/editor_plugin.hpp>
#include <godot_cpp/classes/editor_syntax_highlighter.hpp>
#include <godot_cpp/templates/hash_set.hpp>
#include <godot_cpp/variant/color.hpp>

#include <cstdint>
#include <vector>

namespace luau {

using namespace godot;

class LuauHighlighter : public EditorSyntaxHighlighter {
	GDCLASS(LuauHighlighter, EditorSyntaxHighlighter);

public:
	// Where a line starts: in code, a block comment or long string (with its
	// level of '='), or (Fennel) a string
	struct State {
		enum Kind : uint8_t { CODE, BLOCK_COMMENT, LONG_STRING, STRING } kind = CODE;
		uint8_t level = 0;
		bool operator==(const State &other) const { return kind == other.kind && level == other.level; }
	};
	enum Mode : uint8_t { UNKNOWN, LUAU, FENNEL };

	String _get_name() const override { return "Luau"; }
	PackedStringArray _get_supported_languages() const override { return PackedStringArray({ "Luau" }); }
	Ref<EditorSyntaxHighlighter> _create() const override;
	Dictionary _get_line_syntax_highlighting(int32_t p_line) const override;
	void _clear_highlighting_cache() override;
	void _update_cache() override;

	// The colours for one line, starting in `state`; returns the next line's
	// state. Bound (with the mode) for tests.
	State highlight(const String &p_line, State p_state, Mode p_mode, Dictionary *r_colors) const;
	Dictionary highlight_text(const String &p_text, bool p_fennel);

protected:
	static void _bind_methods();

private:
	Mode mode() const;
	State state_before(int line) const;

	mutable Mode resolved = UNKNOWN;
	mutable std::vector<State> line_states;  // line_states[i]: where line i starts
	Color text, symbol, keyword, control_flow, comment, string, number, function, member, engine_type, base_type;
	HashSet<String> engine_types, base_types;
};

// Registers the highlighter with the script editor
class LuauEditorPlugin : public EditorPlugin {
	GDCLASS(LuauEditorPlugin, EditorPlugin);

public:
	void _enter_tree() override;
	void _exit_tree() override;

protected:
	static void _bind_methods() {}

private:
	Ref<LuauHighlighter> highlighter;
};

} // namespace luau
