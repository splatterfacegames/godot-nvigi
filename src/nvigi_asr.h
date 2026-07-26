// SPDX-FileCopyrightText: Copyright (c) 2026 godot-nvigi contributors
// SPDX-License-Identifier: MIT

#ifndef GDNVIGI_ASR_H
#define GDNVIGI_ASR_H

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/dictionary.hpp>

#include <string>

#include <nvigi.h>
#include <nvigi_ai.h>
#include <nvigi_asr_whisper.h>

namespace godot {

// Speech-to-text through the NVIGI Whisper (GGML) plugin.
//
// Typical use:
//   var asr := NvigiASR.new()
//   asr.load_model("C:/models/ggml-tiny.en-q5_1.gguf", {"backend": "cuda"})
//   var result := asr.transcribe_wav("res://sample.wav")
//   print(result.text, " in ", result.ms, " ms")
class NvigiASR : public RefCounted {
    GDCLASS(NvigiASR, RefCounted)

protected:
    static void _bind_methods();

public:
    ~NvigiASR();

    // Loads a whisper.cpp GGML model file (e.g. ggml-tiny.en-q5_1.bin/.gguf).
    // Options (all optional):
    //   backend (String)     - "cuda" (default) or "cpu"
    //   language (String)    - ISO 639-1 code, default "en"
    //   threads (int)        - CPU threads, default 4
    //   vram_budget_mb (int) - default 8192
    //   vram_mb (int)        - VRAM the model claims to need, default 1024
    // Returns an nvigi result code (0 == OK).
    int load_model(const String &model_path, const Dictionary &options = Dictionary());
    void unload_model();
    bool is_model_loaded() const;

    // Blocking transcription of a WAV file (PCM16; resampled/downmixed to
    // 16 kHz mono automatically). Returns:
    //   { ok: bool, text: String, ms: float, error: String }
    Dictionary transcribe_wav(const String &wav_path);

    String get_last_error() const;

private:
    nvigi::IAutoSpeechRecognition *iasr = nullptr;
    nvigi::InferenceInstance *instance = nullptr;
    nvigi::PluginID plugin_id{};
    std::string model_card_json;
    std::string language;
    String last_error;
};

} // namespace godot

#endif // GDNVIGI_ASR_H
