#include "script.h"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/godot.hpp>

using namespace godot;

static luau::LuauLanguage *language = nullptr;
static Ref<luau::LuauLoader> loader;

static void initialize(ModuleInitializationLevel level) {
	if (level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}
	GDREGISTER_CLASS(luau::LuauScript);
	GDREGISTER_CLASS(luau::LuauLanguage);
	GDREGISTER_CLASS(luau::LuauLoader);
	GDREGISTER_INTERNAL_CLASS(luau::LuauCompletion);
	language = memnew(luau::LuauLanguage);
	Engine::get_singleton()->register_script_language(language);
	loader.instantiate();
	ResourceLoader::get_singleton()->add_resource_format_loader(loader);
}

static void uninitialize(ModuleInitializationLevel level) {
	if (level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}
	ResourceLoader::get_singleton()->remove_resource_format_loader(loader);
	loader.unref();
	Engine::get_singleton()->unregister_script_language(language);
	memdelete(language);
	language = nullptr;
}

extern "C" GDExtensionBool GDE_EXPORT godot_luau_init(GDExtensionInterfaceGetProcAddress get_proc_address,
		GDExtensionClassLibraryPtr library, GDExtensionInitialization *r_initialization) {
	GDExtensionBinding::InitObject init(get_proc_address, library, r_initialization);
	init.register_initializer(initialize);
	init.register_terminator(uninitialize);
	init.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);
	return init.init();
}
