// SPDX-FileCopyrightText: Copyright (c) 2026 godot-nvigi contributors
// SPDX-License-Identifier: MIT

#include "nvigi_runtime.h"
#include "nvigi_core_loader.h"

#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/dictionary.hpp>

#include <cstdio>

using namespace godot;

static String format_uid(const nvigi::UID &uid) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "{%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
            uid.data1, uid.data2, uid.data3,
            uid.data4[0], uid.data4[1], uid.data4[2], uid.data4[3],
            uid.data4[4], uid.data4[5], uid.data4[6], uid.data4[7]);
    return String(buf);
}

static String format_version(const nvigi::Version &v) {
    return String::num_int64(v.major) + "." + String::num_int64(v.minor) + "." + String::num_int64(v.build);
}

void NvigiRuntime::_bind_methods() {
    ClassDB::bind_method(D_METHOD("initialize", "sdk_dir", "log_level"), &NvigiRuntime::initialize, DEFVAL(1));
    ClassDB::bind_method(D_METHOD("shutdown"), &NvigiRuntime::shutdown);
    ClassDB::bind_method(D_METHOD("is_initialized"), &NvigiRuntime::is_initialized);
    ClassDB::bind_method(D_METHOD("get_plugins"), &NvigiRuntime::get_plugins);
    ClassDB::bind_method(D_METHOD("get_adapters"), &NvigiRuntime::get_adapters);
    ClassDB::bind_static_method("NvigiRuntime", D_METHOD("result_to_string", "result"), &NvigiRuntime::result_to_string);
}

int NvigiRuntime::initialize(const String &sdk_dir, int log_level) {
    String dir = sdk_dir;
    if (dir.begins_with("res://") || dir.begins_with("user://")) {
        dir = ProjectSettings::get_singleton()->globalize_path(dir);
    }
    CharString utf8 = dir.utf8();
    return (int)gdnvigi::CoreLoader::get().initialize(std::string(utf8.get_data()), (uint32_t)log_level);
}

int NvigiRuntime::shutdown() {
    return (int)gdnvigi::CoreLoader::get().shutdown();
}

bool NvigiRuntime::is_initialized() const {
    return gdnvigi::CoreLoader::get().is_initialized();
}

TypedArray<Dictionary> NvigiRuntime::get_plugins() const {
    TypedArray<Dictionary> out;
    const nvigi::PluginAndSystemInformation *info = gdnvigi::CoreLoader::get().get_info();
    if (!info) {
        return out;
    }
    for (size_t i = 0; i < info->numDetectedPlugins; i++) {
        const nvigi::PluginSpec *spec = info->detectedPlugins[i];
        if (!spec) {
            continue;
        }
        Dictionary d;
        d["id"] = format_uid(spec->id.id);
        d["crc24"] = (int64_t)spec->id.crc24;
        d["name"] = spec->pluginName ? String::utf8(spec->pluginName) : String();
        d["version"] = format_version(spec->pluginVersion);
        d["api_version"] = format_version(spec->pluginAPI);
        d["supported"] = spec->status == nvigi::kResultOk;
        d["status"] = (int64_t)spec->status;
        out.push_back(d);
    }
    return out;
}

TypedArray<Dictionary> NvigiRuntime::get_adapters() const {
    TypedArray<Dictionary> out;
    const nvigi::PluginAndSystemInformation *info = gdnvigi::CoreLoader::get().get_info();
    if (!info) {
        return out;
    }
    for (size_t i = 0; i < info->numDetectedAdapters; i++) {
        const nvigi::AdapterSpec *spec = info->detectedAdapters[i];
        if (!spec) {
            continue;
        }
        Dictionary d;
        d["vendor"] = String(nvigi::vendorIdToString(spec->vendor));
        d["vendor_id"] = (int64_t)spec->vendor;
        d["dedicated_memory_mb"] = (int64_t)spec->dedicatedMemoryInMB;
        d["driver_version"] = format_version(spec->driverVersion);
        d["architecture"] = (int64_t)spec->architecture;
        out.push_back(d);
    }
    return out;
}

String NvigiRuntime::result_to_string(int result) {
    return String(gdnvigi::CoreLoader::result_to_string((nvigi::Result)result));
}
