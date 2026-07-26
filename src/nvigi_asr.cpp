// SPDX-FileCopyrightText: Copyright (c) 2026 godot-nvigi contributors
// SPDX-License-Identifier: MIT

#include "nvigi_asr.h"
#include "nvigi_core_loader.h"
#include "nvigi_file_io.h"

#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/core/class_db.hpp>

#include <nvigi_stl_helpers.h>

#include <chrono>
#include <cstring>
#include <vector>

using namespace godot;

namespace {

std::string to_utf8(const String &s) {
    CharString cs = s.utf8();
    return std::string(cs.get_data());
}

std::string json_escape(const std::string &s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out += c; break;
        }
    }
    return out;
}

String globalized(const String &path) {
    if (path.begins_with("res://") || path.begins_with("user://")) {
        return ProjectSettings::get_singleton()->globalize_path(path);
    }
    return path;
}

// Minimal RIFF/WAVE reader for PCM16 files. Returns false + error on failure.
bool read_wav_pcm16(const PackedByteArray &bytes, std::vector<int16_t> &samples,
        int &sample_rate, int &channels, String &error) {
    const uint8_t *data = bytes.ptr();
    const size_t size = bytes.size();
    if (size < 44 || std::memcmp(data, "RIFF", 4) != 0 || std::memcmp(data + 8, "WAVE", 4) != 0) {
        error = "Not a RIFF/WAVE file.";
        return false;
    }
    size_t pos = 12;
    uint16_t format = 0;
    uint16_t bits_per_sample = 0;
    bool got_fmt = false;
    bool got_data = false;
    while (pos + 8 <= size) {
        const char *chunk_id = (const char *)(data + pos);
        uint32_t chunk_size;
        std::memcpy(&chunk_size, data + pos + 4, 4);
        const size_t body = pos + 8;
        if (std::memcmp(chunk_id, "fmt ", 4) == 0 && body + 16 <= size) {
            uint16_t v16;
            uint32_t v32;
            std::memcpy(&v16, data + body, 2); format = v16;
            std::memcpy(&v16, data + body + 2, 2); channels = v16;
            std::memcpy(&v32, data + body + 4, 4); sample_rate = (int)v32;
            std::memcpy(&v16, data + body + 14, 2); bits_per_sample = v16;
            got_fmt = true;
        } else if (std::memcmp(chunk_id, "data", 4) == 0 && body + chunk_size <= size) {
            if (!got_fmt) {
                error = "WAV data chunk before fmt chunk.";
                return false;
            }
            if ((format != 1 && format != 0xFFFE) || bits_per_sample != 16) {
                error = "Only 16-bit PCM WAV files are supported.";
                return false;
            }
            const size_t count = chunk_size / 2;
            samples.resize(count);
            std::memcpy(samples.data(), data + body, count * 2);
            got_data = true;
            break;
        }
        pos = body + chunk_size + (chunk_size & 1);
    }
    if (!got_data) {
        error = "No PCM data chunk found.";
        return false;
    }
    return true;
}

// Downmix to mono and linearly resample to 16 kHz (what Whisper expects).
std::vector<int16_t> to_mono_16k(const std::vector<int16_t> &in, int sample_rate, int channels) {
    std::vector<int16_t> mono;
    const size_t frames = channels > 0 ? in.size() / channels : 0;
    mono.reserve(frames);
    for (size_t i = 0; i < frames; i++) {
        int32_t acc = 0;
        for (int c = 0; c < channels; c++) {
            acc += in[i * channels + c];
        }
        mono.push_back((int16_t)(acc / channels));
    }
    if (sample_rate == 16000 || mono.empty()) {
        return mono;
    }
    const double ratio = 16000.0 / (double)sample_rate;
    const size_t out_frames = (size_t)((double)mono.size() * ratio);
    std::vector<int16_t> out(out_frames);
    for (size_t i = 0; i < out_frames; i++) {
        const double src = (double)i / ratio;
        const size_t i0 = (size_t)src;
        const size_t i1 = i0 + 1 < mono.size() ? i0 + 1 : i0;
        const double frac = src - (double)i0;
        out[i] = (int16_t)((1.0 - frac) * mono[i0] + frac * mono[i1]);
    }
    return out;
}

struct TranscribeRun {
    std::string text;
    nvigi::InferenceExecutionState final_state = nvigi::kInferenceExecutionStateInvalid;
};

nvigi::InferenceExecutionState asr_inference_callback(
        const nvigi::InferenceExecutionContext *ctx, nvigi::InferenceExecutionState state, void *user_data) {
    auto *run = (TranscribeRun *)user_data;
    if (ctx && ctx->outputs) {
        const nvigi::InferenceDataText *text{};
        if (ctx->outputs->findAndValidateSlot(nvigi::kASRWhisperDataSlotTranscribedText, &text)) {
            const char *utf8 = text->getUTF8Text();
            // Only keep confirmed (pending/done) chunks; partials get replaced.
            if (utf8 && (state == nvigi::kInferenceExecutionStateDataPending ||
                                state == nvigi::kInferenceExecutionStateDone)) {
                run->text += utf8;
            }
        }
    }
    run->final_state = state;
    return state;
}

} // namespace

void NvigiASR::_bind_methods() {
    ClassDB::bind_method(D_METHOD("load_model", "model_path", "options"), &NvigiASR::load_model, DEFVAL(Dictionary()));
    ClassDB::bind_method(D_METHOD("unload_model"), &NvigiASR::unload_model);
    ClassDB::bind_method(D_METHOD("is_model_loaded"), &NvigiASR::is_model_loaded);
    ClassDB::bind_method(D_METHOD("transcribe_wav", "wav_path"), &NvigiASR::transcribe_wav);
    ClassDB::bind_method(D_METHOD("get_last_error"), &NvigiASR::get_last_error);
}

NvigiASR::~NvigiASR() {
    unload_model();
}

int NvigiASR::load_model(const String &model_path, const Dictionary &options) {
    auto &core = gdnvigi::CoreLoader::get();
    if (!core.is_initialized()) {
        last_error = "NVIGI is not initialized; call NvigiRuntime.initialize() first.";
        return (int)nvigi::kResultInvalidState;
    }
    if (instance) {
        unload_model();
    }

    const String backend = options.get("backend", "cuda");
    plugin_id = backend == String("cpu") ? nvigi::plugin::asr::ggml::cpu::kId
                                         : nvigi::plugin::asr::ggml::cuda::kId;

    nvigi::Result result = core.load_interface(plugin_id, &iasr);
    if (result != nvigi::kResultOk) {
        last_error = String("Failed to load ASR plugin interface: ") + String(gdnvigi::CoreLoader::result_to_string(result));
        iasr = nullptr;
        return (int)result;
    }

    std::string path = to_utf8(globalized(model_path).replace("\\", "/"));
    const int64_t vram_mb = options.get("vram_mb", 1024);
    model_card_json = "{\n"
            "  \"name\": \"" + json_escape(to_utf8(model_path.get_file())) + "\",\n"
            "  \"guid\": \"{00000000-0000-0000-0000-000000000002}\",\n"
            "  \"vram\": " + std::to_string(vram_mb) + ",\n"
            "  \"local_files\": [\"" + json_escape(path) + "\"]\n"
            "}";

    language = to_utf8(options.get("language", "en"));

    nvigi::ASRWhisperCreationParameters params{};
    params.language = language.c_str();

    nvigi::CommonCreationParameters common{};
    common.numThreads = (int32_t)(int64_t)options.get("threads", 4);
    common.vramBudgetMB = (size_t)(int64_t)options.get("vram_budget_mb", 8192);
    common.modelGUID = nullptr;
    common.utf8PathToModels = nullptr;
    common.modelCardJSON = model_card_json.c_str();

    nvigi::FileIOCallbacks io{};
    gdnvigi::make_default_file_io(io);

    if (NVIGI_FAILED(chain1, common.chain(params))) {
        last_error = "Parameter chaining failed.";
        return (int)chain1;
    }
    if (NVIGI_FAILED(chain2, common.chain(io))) {
        last_error = "Parameter chaining failed.";
        return (int)chain2;
    }

    result = iasr->createInstance(common, &instance);
    if (result != nvigi::kResultOk || !instance) {
        last_error = String("Failed to create ASR instance: ") + String(gdnvigi::CoreLoader::result_to_string(result));
        core.unload_interface(plugin_id, iasr);
        iasr = nullptr;
        instance = nullptr;
        return (int)result;
    }

    last_error = String();
    return (int)nvigi::kResultOk;
}

void NvigiASR::unload_model() {
    if (instance && iasr) {
        iasr->destroyInstance(instance);
    }
    instance = nullptr;
    if (iasr) {
        gdnvigi::CoreLoader::get().unload_interface(plugin_id, iasr);
        iasr = nullptr;
    }
}

bool NvigiASR::is_model_loaded() const {
    return instance != nullptr;
}

Dictionary NvigiASR::transcribe_wav(const String &wav_path) {
    Dictionary out;
    out["ok"] = false;
    out["text"] = String();
    out["ms"] = 0.0;

    if (!instance) {
        out["error"] = "No model loaded.";
        last_error = out["error"];
        return out;
    }

    PackedByteArray bytes = FileAccess::get_file_as_bytes(wav_path);
    if (bytes.is_empty()) {
        out["error"] = String("Could not read file: ") + wav_path;
        last_error = out["error"];
        return out;
    }

    std::vector<int16_t> samples;
    int sample_rate = 0;
    int channels = 0;
    String parse_error;
    if (!read_wav_pcm16(bytes, samples, sample_rate, channels, parse_error)) {
        out["error"] = parse_error;
        last_error = parse_error;
        return out;
    }

    std::vector<int16_t> mono16k = to_mono_16k(samples, sample_rate, channels);
    nvigi::InferenceDataAudioSTLHelper audio(mono16k, 1);

    std::vector<nvigi::InferenceDataSlot> slots = { { nvigi::kASRWhisperDataSlotAudio, audio } };
    nvigi::InferenceDataSlotArray inputs{ slots.size(), slots.data() };

    nvigi::ASRWhisperRuntimeParameters runtime{};
    runtime.sampling = nvigi::ASRWhisperSamplingStrategy::eGreedy;

    TranscribeRun run;
    nvigi::InferenceExecutionContext ctx{};
    ctx.instance = instance;
    ctx.inputs = &inputs;
    ctx.callback = &asr_inference_callback;
    ctx.callbackUserData = &run;
    ctx.runtimeParameters = runtime;

    const auto t0 = std::chrono::steady_clock::now();
    nvigi::Result result = instance->evaluate(&ctx);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();

    out["ms"] = ms;
    if (result != nvigi::kResultOk) {
        out["error"] = String("evaluate() failed: ") + String(gdnvigi::CoreLoader::result_to_string(result));
        last_error = out["error"];
        return out;
    }
    if (run.final_state != nvigi::kInferenceExecutionStateDone) {
        out["error"] = "Inference did not reach the Done state.";
        last_error = out["error"];
        return out;
    }

    out["ok"] = true;
    out["text"] = String::utf8(run.text.c_str());
    out["error"] = String();
    last_error = String();
    return out;
}

String NvigiASR::get_last_error() const {
    return last_error;
}
