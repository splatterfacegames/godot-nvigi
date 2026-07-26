#!/usr/bin/env python
# SPDX-FileCopyrightText: Copyright (c) 2026 godot-nvigi contributors
# SPDX-License-Identifier: MIT
#
# Builds the godot-nvigi GDExtension. Usage (after `git submodule update
# --init --recursive`):
#
#   scons target=template_release
#
# Only NVIGI *headers* are needed to compile the wrapper (the framework and
# plugin DLLs are loaded at runtime), so this works without the NVIGI SDK
# binaries being present.

import os

env = SConscript("thirdparty/godot-cpp/SConstruct")

env.Append(
    CPPPATH=[
        "src/",
        "thirdparty/nvigi/NVIGI-Core/source/core/nvigi.api/",
        "thirdparty/nvigi/NVIGI-Core/source/utils/nvigi.ai/",
        "thirdparty/nvigi/NVIGI-Plugins/source/plugins/nvigi.gpt/",
        "thirdparty/nvigi/NVIGI-Plugins/source/plugins/nvigi.asr/",
    ]
)

sources = Glob("src/*.cpp")

if env["platform"] == "windows":
    suffix = env["suffix"].replace(".dev", "").replace(".universal", "")
    lib_path = "demo/addons/godot_nvigi/bin/gdnvigi{}{}".format(suffix, env["SHLIBSUFFIX"])
    library = env.SharedLibrary(lib_path, source=sources)
else:
    raise Exception("godot-nvigi v0.1 only supports Windows x64 (NVIGI SDK requirement).")

Default(library)
