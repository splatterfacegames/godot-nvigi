// SPDX-FileCopyrightText: Copyright (c) 2026 godot-nvigi contributors
// SPDX-License-Identifier: MIT
//
// Process-global loader for the NVIGI core framework.
//
// NVIGI is initialized at most once per process. All wrapper classes
// (NvigiRuntime, NvigiLLM, NvigiASR, ...) talk to this singleton instead of
// touching nvigi.core.framework.dll directly. New plugin wrappers should:
//
//   1. Call CoreLoader::get().load_interface<T>(plugin_id, &iface) to obtain
//      the plugin interface (reference counted by NVIGI).
//   2. Call CoreLoader::get().unload_interface(plugin_id, iface) when done.
//
// See NvigiLLM / NvigiASR for the reference pattern.

#ifndef GDNVIGI_CORE_LOADER_H
#define GDNVIGI_CORE_LOADER_H

#include <string>

#include <nvigi.h>
#include <nvigi_types.h>
#include <nvigi_result.h>

namespace gdnvigi {

class CoreLoader {
public:
    static CoreLoader &get();

    // Loads nvigi.core.framework.dll from `sdk_dir_utf8` and calls nvigiInit
    // with `sdk_dir_utf8` as the plugin search path.
    // Returns nvigi::kResultOk (0) on success or an nvigi result code.
    nvigi::Result initialize(const std::string &sdk_dir_utf8, uint32_t log_level);

    nvigi::Result shutdown();

    bool is_initialized() const { return initialized; }

    const nvigi::PluginAndSystemInformation *get_info() const { return info; }

    const std::string &get_sdk_dir() const { return sdk_dir; }

    template <typename T>
    nvigi::Result load_interface(nvigi::PluginID id, T **iface) {
        if (!initialized || !fn_load_interface) {
            return nvigi::kResultInvalidState;
        }
        return nvigiGetInterfaceDynamic(id, iface, fn_load_interface);
    }

    nvigi::Result unload_interface(nvigi::PluginID id, void *iface);

    // Human readable name for an nvigi result code.
    static const char *result_to_string(nvigi::Result result);

private:
    CoreLoader() = default;

    bool initialized = false;
    void *library = nullptr; // HMODULE
    std::string sdk_dir;

    PFun_nvigiInit *fn_init = nullptr;
    PFun_nvigiShutdown *fn_shutdown = nullptr;
    PFun_nvigiLoadInterface *fn_load_interface = nullptr;
    PFun_nvigiUnloadInterface *fn_unload_interface = nullptr;

    nvigi::PluginAndSystemInformation *info = nullptr;
};

} // namespace gdnvigi

#endif // GDNVIGI_CORE_LOADER_H
