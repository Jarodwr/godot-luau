// The script editor's highlighter for Luau and Fennel (docs/adr/0053).
//
// Godot asks for one line at a time; a line's colours depend on where it
// starts (inside a block comment, a long string, or a Fennel string), so each
// line's starting state is cached, computed forward from the first line
// that isn't known, and dropped when the text changes.
#include "highlighter.h"

#include <godot_cpp/classes/editor_interface.hpp>
#include <godot_cpp/classes/editor_settings.hpp>
#include <godot_cpp/classes/script.hpp>
#include <godot_cpp/classes/script_editor.hpp>
#include <godot_cpp/classes/script_editor_base.hpp>
#include <godot_cpp/classes/text_edit.hpp>
#include <godot_cpp/core/class_db.hpp>

namespace luau {

namespace {

const char *const LUAU_KEYWORDS[] = { "and", "false", "function", "local", "nil", "not", "or", "true", "self", "type", "export" };
const char *const LUAU_CONTROL_FLOW[] = { "break", "continue", "do", "else", "elseif", "end", "for", "if", "in", "repeat", "return", "then", "until", "while" };
const char *const FENNEL_SPECIALS[] = { "fn", "lambda", "λ", "local", "var", "global", "set", "tset", "let", "values", "not", "and", "or", "length",
	"macro", "macros", "import-macros", "require-macros", "include", "quote", "hashfn", "partial", "pick-values", "doto", "->", "->>", "-?>", "-?>>",
	"..", "#", "accumulate", "faccumulate", "icollect", "collect", "fcollect", "comment", "eval-compiler", "lua" };
const char *const FENNEL_CONTROL_FLOW[] = { "if", "when", "each", "for", "while", "do", "match", "case", "match-try", "case-try", "where", "or" };
const char *const LITERALS[] = { "true", "false", "nil" };

template <size_t N>
bool one_of(const String &word, const char *const (&words)[N]) {
	for (const char *w : words) {
		if (word == String::utf8(w)) {
			return true;
		}
	}
	return false;
}

bool is_identifier_start(char32_t c) {
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c > 127;
}

bool is_identifier(char32_t c) {
	return is_identifier_start(c) || (c >= '0' && c <= '9');
}

bool is_digit(char32_t c) {
	return c >= '0' && c <= '9';
}

// Fennel symbols end at whitespace and delimiters
bool is_fennel_symbol(char32_t c) {
	return c > ' ' && c != '(' && c != ')' && c != '[' && c != ']' && c != '{' && c != '}' && c != '"' && c != ';' && c != '`' && c != ','
			&& c != '\'' && c != '@' && c != '~';
}

// "[", "=" * level, "[" at i: the level, or -1
int long_bracket(const String &line, int i) {
	if (i >= line.length() || line[i] != '[') {
		return -1;
	}
	int j = i + 1;
	while (j < line.length() && line[j] == '=') {
		j++;
	}
	return j < line.length() && line[j] == '[' ? j - i - 1 : -1;
}

// The index after "]", "=" * level, "]" from i, or -1
int long_bracket_end(const String &line, int i, int level) {
	for (; i < line.length(); i++) {
		if (line[i] != ']') {
			continue;
		}
		int j = i + 1, n = 0;
		while (j < line.length() && line[j] == '=') {
			j++;
			n++;
		}
		if (n == level && j < line.length() && line[j] == ']') {
			return j + 1;
		}
	}
	return -1;
}

int skip_spaces(const String &line, int i) {
	while (i < line.length() && (line[i] == ' ' || line[i] == '\t')) {
		i++;
	}
	return i;
}

} // namespace

Ref<EditorSyntaxHighlighter> LuauHighlighter::_create() const {
	Ref<LuauHighlighter> highlighter;
	highlighter.instantiate();
	return highlighter;
}

void LuauHighlighter::_update_cache() {
	Ref<EditorSettings> settings = EditorInterface::get_singleton()->get_editor_settings();
	auto color = [&](const char *name) {
		return (Color)settings->get_setting(String("text_editor/theme/highlighting/") + name);
	};
	text = color("text_color");
	symbol = color("symbol_color");
	keyword = color("keyword_color");
	control_flow = color("control_flow_keyword_color");
	comment = color("comment_color");
	string = color("string_color");
	number = color("number_color");
	function = color("function_color");
	member = color("member_variable_color");
	engine_type = color("engine_type_color");
	base_type = color("base_type_color");
	if (engine_types.is_empty()) {
		for (const String &name : ClassDB::get_class_list()) {
			engine_types.insert(name);
		}
		for (int t = Variant::NIL + 1; t < Variant::VARIANT_MAX; t++) {
			String name = Variant::get_type_name((Variant::Type)t);
			if (name != "bool" && name != "int" && name != "float" && name != "String" && name != "Object") {
				base_types.insert(name);
			}
		}
	}
	_clear_highlighting_cache();
}

void LuauHighlighter::_clear_highlighting_cache() {
	line_states.clear();
}

// The edited script's kind: from its file, once the script editor lists the
// editor this highlighter is in; until then, from its first line (Fennel
// files start with a comment ";" or a form "(")
LuauHighlighter::Mode LuauHighlighter::mode() const {
	if (resolved != UNKNOWN) {
		return resolved;
	}
	TextEdit *edit = get_text_edit();
	if (edit == nullptr) {
		return LUAU;
	}
	if (EditorInterface *editor = EditorInterface::get_singleton()) {
		ScriptEditor *scripts = editor->get_script_editor();
		TypedArray<ScriptEditorBase> editors = scripts->get_open_script_editors();
		Array open = scripts->get_open_scripts();
		for (int i = 0; i < editors.size() && i < open.size(); i++) {
			ScriptEditorBase *base = Object::cast_to<ScriptEditorBase>(editors[i]);
			Ref<Script> script = open[i];
			if (base && base->get_base_editor() == edit && script.is_valid()) {
				resolved = script->get_path().get_extension() == "fnl" ? FENNEL : LUAU;
				return resolved;
			}
		}
	}
	for (int i = 0; i < edit->get_line_count(); i++) {
		String first = edit->get_line(i).strip_edges();
		if (!first.is_empty()) {
			return first.begins_with(";") || first.begins_with("(") ? FENNEL : LUAU;
		}
	}
	return LUAU;
}

LuauHighlighter::State LuauHighlighter::state_before(int line) const {
	TextEdit *edit = get_text_edit();
	if (line_states.empty()) {
		line_states.push_back(State());
	}
	Mode m = mode();
	while ((int)line_states.size() <= line) {
		int known = (int)line_states.size() - 1;
		line_states.push_back(highlight(edit->get_line(known), line_states[known], m, nullptr));
	}
	return line_states[line];
}

Dictionary LuauHighlighter::_get_line_syntax_highlighting(int32_t p_line) const {
	Dictionary colors;
	TextEdit *edit = get_text_edit();
	if (edit == nullptr || p_line < 0 || p_line >= edit->get_line_count()) {
		return colors;
	}
	State start = state_before(p_line);
	State next = highlight(edit->get_line(p_line), start, mode(), &colors);
	if ((int)line_states.size() > p_line + 1 && !(line_states[p_line + 1] == next)) {
		line_states.resize(p_line + 1);  // later lines start differently now
	}
	return colors;
}

LuauHighlighter::State LuauHighlighter::highlight(const String &line, State state, Mode m, Dictionary *r_colors) const {
	Color current;
	bool any = false;
	auto paint = [&](int column, const Color &c) {
		if (r_colors && (!any || c != current)) {
			Dictionary d;
			d["color"] = c;
			(*r_colors)[column] = d;
			current = c;
			any = true;
		}
	};
	int n = line.length();
	int i = 0;

	// Continuing a multi-line construct
	if (state.kind == State::BLOCK_COMMENT || state.kind == State::LONG_STRING) {
		paint(0, state.kind == State::BLOCK_COMMENT ? comment : string);
		int end = long_bracket_end(line, 0, state.level);
		if (end < 0) {
			return state;
		}
		i = end;
		state = State();
	} else if (state.kind == State::STRING) {
		paint(0, string);
		for (; i < n; i++) {
			if (line[i] == '\\') {
				i++;
			} else if (line[i] == '"') {
				break;
			}
		}
		if (i >= n) {
			return state;
		}
		i++;
		state = State();
	}

	char32_t previous = 0;  // the last non-space character before the token
	int previous_end = -1;  // where it ended
	while (i < n) {
		char32_t c = line[i];
		if (c == ' ' || c == '\t') {
			i++;
			continue;
		}
		int start = i;
		if (m == FENNEL) {
			if (c == ';') {
				paint(i, comment);
				return state;
			}
			if (c == '"') {
				paint(i, string);
				for (i++; i < n; i++) {
					if (line[i] == '\\') {
						i++;
					} else if (line[i] == '"') {
						break;
					}
				}
				if (i >= n) {
					state.kind = State::STRING;
					return state;
				}
				i++;
			} else if (c == ':' && i + 1 < n && is_fennel_symbol(line[i + 1])) {
				paint(i, string);  // :keyword is a string
				for (i++; i < n && is_fennel_symbol(line[i]); i++) {
				}
			} else if (is_digit(c) || (c == '-' && i + 1 < n && is_digit(line[i + 1]))) {
				paint(i, number);
				for (i++; i < n && is_fennel_symbol(line[i]); i++) {
				}
			} else if (is_fennel_symbol(c)) {
				for (; i < n && is_fennel_symbol(line[i]); i++) {
				}
				String word = line.substr(start, i - start);
				if (one_of(word, LITERALS)) {
					paint(start, keyword);
				} else if (previous == '(' && one_of(word, FENNEL_CONTROL_FLOW)) {
					paint(start, control_flow);
				} else if (previous == '(' && one_of(word, FENNEL_SPECIALS)) {
					paint(start, keyword);
				} else if (previous == '(') {
					paint(start, function);
				} else {
					int dot = word.find(".");
					String head = dot > 0 ? word.substr(0, dot) : word;
					paint(start, engine_types.has(head) ? engine_type : (base_types.has(head) ? base_type : text));
				}
			} else {
				paint(i, symbol);
				i++;
			}
			previous = c == '(' ? '(' : 0;  // the next symbol heads a form
			continue;
		}

		// Luau
		if (c == '-' && i + 1 < n && line[i + 1] == '-') {
			paint(i, comment);
			int level = long_bracket(line, i + 2);
			if (level < 0) {
				return state;  // a line comment
			}
			int end = long_bracket_end(line, i + 2 + level + 2, level);
			if (end < 0) {
				state.kind = State::BLOCK_COMMENT;
				state.level = (uint8_t)level;
				return state;
			}
			i = end;
		} else if (c == '"' || c == '\'' || c == '`') {
			paint(i, string);
			for (i++; i < n; i++) {
				if (line[i] == '\\') {
					i++;
				} else if (line[i] == c) {
					break;
				}
			}
			i = i < n ? i + 1 : n;
		} else if (int level = long_bracket(line, i); level >= 0) {
			paint(i, string);
			int end = long_bracket_end(line, i + level + 2, level);
			if (end < 0) {
				state.kind = State::LONG_STRING;
				state.level = (uint8_t)level;
				return state;
			}
			i = end;
		} else if (is_digit(c) || (c == '.' && i + 1 < n && is_digit(line[i + 1]))) {
			paint(i, number);
			for (i++; i < n; i++) {
				char32_t d = line[i];
				bool exponent_sign = (d == '+' || d == '-') && (line[i - 1] == 'e' || line[i - 1] == 'E');
				if (!(is_identifier(d) || d == '.' || exponent_sign)) {
					break;
				}
			}
		} else if (is_identifier_start(c)) {
			for (; i < n && is_identifier(line[i]); i++) {
			}
			String word = line.substr(start, i - start);
			int after = skip_spaces(line, i);
			char32_t next = after < n ? line[after] : 0;
			bool call = next == '(' || next == '"' || next == '{' || next == '\'' || next == '`';
			// x.y and v:m() touch their '.' or ':'; "n: Node" is a type annotation
			bool field = (previous == '.' || previous == ':') && start == previous_end;
			if (!field && one_of(word, LUAU_CONTROL_FLOW)) {
				paint(start, control_flow);
			} else if (!field && one_of(word, LUAU_KEYWORDS)) {
				paint(start, keyword);
			} else if (field) {
				paint(start, call ? function : member);
			} else if (engine_types.has(word)) {
				paint(start, engine_type);
			} else if (base_types.has(word)) {
				paint(start, base_type);
			} else {
				paint(start, call ? function : text);
			}
		} else {
			paint(i, symbol);
			i++;
		}
		previous = line[i - 1];
		previous_end = i;
		if (previous != '.' && previous != ':') {
			previous = 0;
		}
	}
	return state;
}

// For tests: each line's colours as {column: "kind"}, by comparing with the
// theme's colours (lines are keys)
Dictionary LuauHighlighter::highlight_text(const String &p_text, bool p_fennel) {
	if (engine_types.is_empty()) {
		_update_cache();
	}
	Dictionary result;
	State state;
	PackedStringArray lines = p_text.split("\n");
	const std::pair<const Color *, const char *> names[] = { { &comment, "comment" }, { &string, "string" }, { &number, "number" },
		{ &control_flow, "control_flow" }, { &keyword, "keyword" }, { &function, "function" }, { &member, "member" },
		{ &engine_type, "engine_type" }, { &base_type, "base_type" }, { &symbol, "symbol" }, { &text, "text" } };
	for (int l = 0; l < lines.size(); l++) {
		Dictionary colors;
		state = highlight(lines[l], state, p_fennel ? FENNEL : LUAU, &colors);
		Dictionary kinds;
		for (const Variant &column : colors.keys()) {
			Color c = ((Dictionary)colors[column])["color"];
			for (const auto &[color, name] : names) {
				if (*color == c) {
					kinds[column] = name;
					break;
				}
			}
		}
		result[l] = kinds;
	}
	return result;
}

void LuauHighlighter::_bind_methods() {
	ClassDB::bind_method(D_METHOD("highlight_text", "text", "fennel"), &LuauHighlighter::highlight_text);
}

void LuauEditorPlugin::_enter_tree() {
	highlighter.instantiate();
	EditorInterface::get_singleton()->get_script_editor()->register_syntax_highlighter(highlighter);
}

void LuauEditorPlugin::_exit_tree() {
	if (highlighter.is_valid()) {
		EditorInterface::get_singleton()->get_script_editor()->unregister_syntax_highlighter(highlighter);
		highlighter.unref();
	}
}

} // namespace luau
