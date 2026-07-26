// SPDX-FileCopyrightText: Copyright (c) 2026 godot-nvigi contributors
// SPDX-License-Identifier: MIT

#ifndef GDNVIGI_RUNTIME_H
#define GDNVIGI_RUNTIME_H

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/typed_array.hpp>

namespace godot {

// Init / shutdown of the NVIGI core framework plus plugin & adapter
// enumeration. The underlying framework is a process-wide singleton;
// creating several NvigiRuntime objects is harmless.
class NvigiRuntime : public RefCounted {
    GDCLASS(NvigiRuntime, RefCounted)

protected:
    static void _bind_methods();

public:
    // sdk_dir: directory containing nvigi.core.framework.dll and the plugin
    // DLLs. Accepts res:// paths. log_level: 0 = off, 1 = default, 2 = verbose.
    // Returns an nvigi result code (0 == OK).
    int initialize(const String &sdk_dir, int log_level = 1);
    int shutdown();
    bool is_initialized() const;

    // Array of Dictionaries: { id: String (GUID), crc24: int, name: String,
    //   version: String, api_version: String, supported: bool, status: int }
    TypedArray<Dictionary> get_plugins() const;

    // Array of Dictionaries: { vendor: String, vendor_id: int,
    //   dedicated_memory_mb: int, driver_version: String, architecture: int }
    TypedArray<Dictionary> get_adapters() const;

    // Convenience: human readable string for any nvigi result code returned
    // by this extension.
    static String result_to_string(int result);
};

} // namespace godot

#endif // GDNVIGI_RUNTIME_H
