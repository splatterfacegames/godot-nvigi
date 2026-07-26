// SPDX-FileCopyrightText: Copyright (c) 2026 godot-nvigi contributors
// SPDX-License-Identifier: MIT

#include "nvigi_core_loader.h"

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <godot_cpp/variant/utility_functions.hpp>

namespace gdnvigi {

static std::wstring utf8_to_wide(const std::string &utf8) {
    if (utf8.empty()) {
        return std::wstring();
    }
    int len = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), nullptr, 0);
    std::wstring wide(len, 0);
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), wide.data(), len);
    return wide;
}

static void log_message_callback(nvigi::LogType type, const char *msg) {
    if (!msg) {
        return;
    }
    // NVIGI messages already contain a trailing newline; strip it for Godot's log.
    std::string text(msg);
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
        text.pop_back();
    }
    switch (type) {
        case nvigi::LogType::eError:
            godot::UtilityFunctions::printerr(godot::String("[nvigi] ") + godot::String::utf8(text.c_str()));
            break;
        case nvigi::LogType::eWarn:
            godot::UtilityFunctions::push_warning(godot::String("[nvigi] ") + godot::String::utf8(text.c_str()));
            break;
        default:
            godot::UtilityFunctions::print_verbose(godot::String("[nvigi] ") + godot::String::utf8(text.c_str()));
            break;
    }
}

CoreLoader &CoreLoader::get() {
    static CoreLoader loader;
    return loader;
}

nvigi::Result CoreLoader::initialize(const std::string &sdk_dir_utf8, uint32_t log_level) {
    if (initialized) {
        return nvigi::kResultOk;
    }

    const std::wstring dir = utf8_to_wide(sdk_dir_utf8);
    const std::wstring dll_path = dir + L"\\nvigi.core.framework.dll";

    // Make sure dependent DLLs living next to the framework can be resolved.
    SetDllDirectoryW(dir.c_str());
    HMODULE module = LoadLibraryW(dll_path.c_str());
    if (!module) {
        godot::UtilityFunctions::printerr("[gdnvigi] Failed to load nvigi.core.framework.dll from: ",
                godot::String::utf8(sdk_dir_utf8.c_str()), " (error ", (int64_t)GetLastError(), ")");
        SetDllDirectoryW(nullptr);
        return nvigi::kResultMissingDynamicLibraryDependency;
    }

    fn_init = reinterpret_cast<PFun_nvigiInit *>(GetProcAddress(module, "nvigiInit"));
    fn_shutdown = reinterpret_cast<PFun_nvigiShutdown *>(GetProcAddress(module, "nvigiShutdown"));
    fn_load_interface = reinterpret_cast<PFun_nvigiLoadInterface *>(GetProcAddress(module, "nvigiLoadInterface"));
    fn_unload_interface = reinterpret_cast<PFun_nvigiUnloadInterface *>(GetProcAddress(module, "nvigiUnloadInterface"));

    if (!fn_init || !fn_shutdown || !fn_load_interface || !fn_unload_interface) {
        godot::UtilityFunctions::printerr("[gdnvigi] nvigi.core.framework.dll is missing expected exports.");
        FreeLibrary(module);
        SetDllDirectoryW(nullptr);
        return nvigi::kResultMissingDynamicLibraryDependency;
    }

    sdk_dir = sdk_dir_utf8;
    const char *plugin_paths[] = { sdk_dir.c_str() };

    nvigi::Preferences pref{};
    pref.showConsole = false;
    pref.logLevel = (nvigi::LogLevel)(log_level < (uint32_t)nvigi::LogLevel::eCount ? log_level : 1u);
    pref.utf8PathsToPlugins = plugin_paths;
    pref.numPathsToPlugins = 1;
    pref.utf8PathToLogsAndData = nullptr; // no log file by default
    pref.logMessageCallback = &log_message_callback;

    nvigi::Result result = fn_init(pref, &info, nvigi::kSDKVersion);
    SetDllDirectoryW(nullptr);

    if (result != nvigi::kResultOk) {
        godot::UtilityFunctions::printerr("[gdnvigi] nvigiInit failed: ", result_to_string(result));
        FreeLibrary(module);
        fn_init = nullptr;
        fn_shutdown = nullptr;
        fn_load_interface = nullptr;
        fn_unload_interface = nullptr;
        info = nullptr;
        return result;
    }

    library = module;
    initialized = true;
    return nvigi::kResultOk;
}

nvigi::Result CoreLoader::shutdown() {
    if (!initialized) {
        return nvigi::kResultOk;
    }
    nvigi::Result result = fn_shutdown();
    if (library) {
        FreeLibrary((HMODULE)library);
        library = nullptr;
    }
    fn_init = nullptr;
    fn_shutdown = nullptr;
    fn_load_interface = nullptr;
    fn_unload_interface = nullptr;
    info = nullptr;
    initialized = false;
    return result;
}

nvigi::Result CoreLoader::unload_interface(nvigi::PluginID id, void *iface) {
    if (!initialized || !fn_unload_interface) {
        return nvigi::kResultInvalidState;
    }
    return fn_unload_interface(id, iface);
}

const char *CoreLoader::result_to_string(nvigi::Result result) {
    switch (result) {
        case nvigi::kResultOk: return "ok";
        case nvigi::kResultDriverOutOfDate: return "driver out of date";
        case nvigi::kResultOSOutOfDate: return "OS out of date";
        case nvigi::kResultNoPluginsFound: return "no plugins found";
        case nvigi::kResultInvalidParameter: return "invalid parameter";
        case nvigi::kResultNoSupportedHardwareFound: return "no supported hardware found";
        case nvigi::kResultMissingInterface: return "missing interface";
        case nvigi::kResultMissingDynamicLibraryDependency: return "missing dynamic library dependency";
        case nvigi::kResultInvalidState: return "invalid state";
        case nvigi::kResultException: return "exception";
        case nvigi::kResultJSONException: return "JSON exception";
        case nvigi::kResultRPCError: return "RPC error";
        case nvigi::kResultInsufficientResources: return "insufficient resources (VRAM budget too low?)";
        case nvigi::kResultNotReady: return "not ready";
        case nvigi::kResultPluginOutOfDate: return "plugin out of date";
        case nvigi::kResultDuplicatedPluginId: return "duplicated plugin id";
        case nvigi::kResultNoImplementation: return "no implementation";
        case nvigi::kResultItemNotFound: return "item not found";
        case nvigi::kResultTimedOut: return "timed out";
        default: return "unknown nvigi error";
    }
}

} // namespace gdnvigi
