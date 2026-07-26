// SPDX-FileCopyrightText: Copyright (c) 2026 godot-nvigi contributors
// SPDX-License-Identifier: MIT

#include "register_types.h"

#include "nvigi_asr.h"
#include "nvigi_core_loader.h"
#include "nvigi_llm.h"
#include "nvigi_runtime.h"

#include <gdextension_interface.h>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/godot.hpp>

using namespace godot;

void initialize_gdnvigi_module(ModuleInitializationLevel p_level) {
    if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
        return;
    }
    GDREGISTER_CLASS(NvigiRuntime);
    GDREGISTER_CLASS(NvigiLLM);
    GDREGISTER_CLASS(NvigiASR);
}

void uninitialize_gdnvigi_module(ModuleInitializationLevel p_level) {
    if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
        return;
    }
    // Make sure the framework is torn down before the DLL unloads.
    gdnvigi::CoreLoader::get().shutdown();
}

extern "C" {
GDExtensionBool GDE_EXPORT gdnvigi_library_init(GDExtensionInterfaceGetProcAddress p_get_proc_address,
        const GDExtensionClassLibraryPtr p_library, GDExtensionInitialization *r_initialization) {
    godot::GDExtensionBinding::InitObject init_obj(p_get_proc_address, p_library, r_initialization);

    init_obj.register_initializer(initialize_gdnvigi_module);
    init_obj.register_terminator(uninitialize_gdnvigi_module);
    init_obj.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);

    return init_obj.init();
}
}
