// SPDX-FileCopyrightText: Copyright (c) 2026 godot-nvigi contributors
// SPDX-License-Identifier: MIT
//
// Default stdio-based nvigi::FileIOCallbacks implementation. Required when
// loading models through an embedded model card JSON (CommonCreationParameters
// ::modelCardJSON), which is how this wrapper points NVIGI at arbitrary
// GGUF/GGML files on disk.

#ifndef GDNVIGI_FILE_IO_H
#define GDNVIGI_FILE_IO_H

#include <nvigi_io.h>

namespace gdnvigi {

// Fills `callbacks` with a stdio implementation (UTF-8 paths, _wfopen on
// Windows). The returned callbacks have no state; userData stays null.
void make_default_file_io(nvigi::FileIOCallbacks &callbacks);

} // namespace gdnvigi

#endif // GDNVIGI_FILE_IO_H
