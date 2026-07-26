// SPDX-FileCopyrightText: Copyright (c) 2026 godot-nvigi contributors
// SPDX-License-Identifier: MIT

#include "nvigi_file_io.h"

#include <cstdio>
#include <string>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace gdnvigi {

static void *io_open(void *, const char *fname, const char *mode) {
    if (!fname || !mode) {
        return nullptr;
    }
#ifdef _WIN32
    // fname is UTF-8; convert for Windows.
    int wlen = MultiByteToWideChar(CP_UTF8, 0, fname, -1, nullptr, 0);
    std::wstring wname(wlen, 0);
    MultiByteToWideChar(CP_UTF8, 0, fname, -1, wname.data(), wlen);
    std::wstring wmode(mode, mode + std::string(mode).size());
    return (void *)_wfopen(wname.c_str(), wmode.c_str());
#else
    return (void *)fopen(fname, mode);
#endif
}

static void io_close(void *, void *handle) {
    if (handle) {
        fclose((FILE *)handle);
    }
}

static size_t io_size(void *, void *handle) {
    if (!handle) {
        return 0;
    }
    FILE *f = (FILE *)handle;
#ifdef _WIN32
    __int64 pos = _ftelli64(f);
    _fseeki64(f, 0, SEEK_END);
    __int64 size = _ftelli64(f);
    _fseeki64(f, pos, SEEK_SET);
    return (size_t)size;
#else
    long pos = ftell(f);
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, pos, SEEK_SET);
    return (size_t)size;
#endif
}

static size_t io_tell(void *, void *handle) {
    if (!handle) {
        return 0;
    }
#ifdef _WIN32
    return (size_t)_ftelli64((FILE *)handle);
#else
    return (size_t)ftell((FILE *)handle);
#endif
}

static int io_seek(void *, void *handle, size_t offset, int whence) {
    if (!handle) {
        return -1;
    }
#ifdef _WIN32
    return _fseeki64((FILE *)handle, (__int64)offset, whence);
#else
    return fseek((FILE *)handle, (long)offset, whence);
#endif
}

static size_t io_read(void *, void *handle, void *ptr, size_t len) {
    if (!handle || !ptr) {
        return 0;
    }
    return fread(ptr, 1, len, (FILE *)handle);
}

static size_t io_write(void *, void *handle, const void *ptr, size_t len) {
    if (!handle || !ptr) {
        return 0;
    }
    return fwrite(ptr, 1, len, (FILE *)handle);
}

void make_default_file_io(nvigi::FileIOCallbacks &callbacks) {
    callbacks.userData = nullptr;
    callbacks.open = &io_open;
    callbacks.close = &io_close;
    callbacks.size = &io_size;
    callbacks.tell = &io_tell;
    callbacks.seek = &io_seek;
    callbacks.read = &io_read;
    callbacks.write = &io_write;
    callbacks.map = nullptr;
    callbacks.unmap = nullptr;
    callbacks.getLastError = nullptr;
}

} // namespace gdnvigi
