// Script errors reported as Godot script errors (docs/adr/0037): the message,
// the innermost script location (file and line, which the editor's debugger
// links to), and a backtrace of the Luau frames. Errors in calls from Godot
// are reported after the coroutine failed: Luau keeps a failed coroutine's
// frames, so nothing runs while there is no error.
#include "internal.h"

#include <godot_cpp/godot.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <lualib.h>

using namespace godot;

namespace luau {

namespace {

struct Frame {
	String file;
	int line;
	String function;
};

// "res://a.luau:12: message" -> file, line, message (Luau runtime and syntax
// errors, Fennel compile errors with "file:line:column:")
bool split_location(const String &text, String &r_file, int &r_line, String &r_message) {
	if (!text.begins_with("res://")) {
		return false;
	}
	int colon = text.find(": ");
	if (colon < 0) {
		return false;
	}
	PackedStringArray parts = text.substr(0, colon).split(":");  // res, //path, line[, column]
	if (parts.size() < 3 || !parts[2].is_valid_int()) {
		return false;
	}
	r_file = parts[0] + ":" + parts[1];
	r_line = parts[2].to_int();
	r_message = text.substr(colon + 2);
	return true;
}

void print(const String &description, const String &function, const String &file, int line) {
	CharString d = description.utf8(), fn = function.utf8(), f = file.utf8();
	gdextension_interface::print_script_error(d.get_data(), fn.get_data(), f.get_data(), line, true);
}

} // namespace

void report_error(lua_State *thread, int level, const char *message) {
	String text = message ? String::utf8(message) : String("error (not a string)");
	std::vector<Frame> frames;
	lua_Debug ar;
	for (int i = level; lua_getinfo(thread, i, "sln", &ar); i++) {
		if (ar.source[0] != '@' || ar.currentline < 0) {
			continue;  // C functions: Godot's own frames aren't shown either
		}
		String name = ar.name ? String::utf8(ar.name) : "function at line " + itos(ar.linedefined);
		frames.push_back({ String::utf8(ar.source + 1), ar.currentline, name });
	}
	String file, rest;
	int line = 0;
	if (split_location(text, file, line, rest)) {
		text = rest;  // the location is printed separately
	}
	if (frames.empty()) {
		print(text, "", file, line);
		return;
	}
	if (frames.size() == 1) {
		print(text, frames[0].function, frames[0].file, frames[0].line);  // "at:" says it all
		return;
	}
	String description = text + "\nLuau backtrace (most recent call first):";
	for (size_t i = 0; i < frames.size(); i++) {
		description += "\n    [" + itos(i) + "] " + frames[i].function + " (" + frames[i].file + ":" + itos(frames[i].line) + ")";
	}
	print(description, frames[0].function, frames[0].file, frames[0].line);
}

void report_message(const String &message) {
	String file, rest;
	int line = 0;
	if (split_location(message, file, line, rest)) {
		print(rest, "load", file, line);
	} else {
		UtilityFunctions::push_error(message);
	}
}

// The message handler of godot-luau's own lua_pcall calls: reports while the
// failing frames are still on the stack. The pcall's caller doesn't report
// again.
static int error_handler(lua_State *L) {
	report_error(L, 1, lua_tostring(L, 1));
	return 1;
}

void install_error_handler(lua_State *L) {
	lua_pushcfunction(L, error_handler, "error_handler");
	if (lua_gettop(L) != ERROR_HANDLER) {
		UtilityFunctions::push_error("godot-luau: the error handler must be the main thread's first stack slot");
	}
}

String call_error_text(const GDExtensionCallError &error) {
	switch (error.error) {
		case GDEXTENSION_CALL_ERROR_INVALID_METHOD:
			return "no such method";
		case GDEXTENSION_CALL_ERROR_INVALID_ARGUMENT:
			return "argument " + itos(error.argument + 1) + " should be " + Variant::get_type_name((Variant::Type)error.expected);
		case GDEXTENSION_CALL_ERROR_TOO_MANY_ARGUMENTS:
			return "too many arguments (expected at most " + itos(error.expected) + ")";
		case GDEXTENSION_CALL_ERROR_TOO_FEW_ARGUMENTS:
			return "too few arguments (expected at least " + itos(error.expected) + ")";
		case GDEXTENSION_CALL_ERROR_INSTANCE_IS_NULL:
			return "the object is null";
		case GDEXTENSION_CALL_ERROR_METHOD_NOT_CONST:
			return "the method isn't const";
		default:
			return "error " + itos(error.error);
	}
}

} // namespace luau
