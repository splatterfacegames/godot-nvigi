// SPDX-FileCopyrightText: Copyright (c) 2026 godot-nvigi contributors
// SPDX-License-Identifier: MIT

#include "nvigi_llm.h"
#include "nvigi_core_loader.h"
#include "nvigi_file_io.h"

#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <nvigi_stl_helpers.h>

#include <chrono>
#include <vector>

using namespace godot;

namespace {

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

std::string to_utf8(const String &s) {
    CharString cs = s.utf8();
    return std::string(cs.get_data());
}

String globalized(const String &path) {
    if (path.begins_with("res://") || path.begins_with("user://")) {
        return ProjectSettings::get_singleton()->globalize_path(path);
    }
    return path;
}

double ms_since(const std::chrono::steady_clock::time_point &t0) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

} // namespace

struct NvigiLLM::GenerationRun {
    NvigiLLM *owner = nullptr;
    bool async = false;
    std::string full_text;
    int64_t tokens = 0;
    std::chrono::steady_clock::time_point start;
    double ttft_ms = -1.0;
    nvigi::InferenceExecutionState final_state = nvigi::kInferenceExecutionStateInvalid;
};

static nvigi::InferenceExecutionState llm_inference_callback(
        const nvigi::InferenceExecutionContext *ctx, nvigi::InferenceExecutionState state, void *user_data) {
    auto *run = (NvigiLLM::GenerationRun *)user_data;
    if (ctx && ctx->outputs) {
        const nvigi::InferenceDataText *text{};
        if (ctx->outputs->findAndValidateSlot(nvigi::kGPTDataSlotResponse, &text)) {
            const char *utf8 = text->getUTF8Text();
            if (utf8 && utf8[0] != '\0') {
                if (run->ttft_ms < 0.0) {
                    run->ttft_ms = std::chrono::duration<double, std::milli>(
                            std::chrono::steady_clock::now() - run->start).count();
                }
                run->tokens++;
                run->full_text += utf8;
                if (run->async) {
                    run->owner->call_deferred("_emit_token", String::utf8(utf8));
                }
            }
        }
    }
    run->final_state = state;
    return state;
}

void NvigiLLM::_bind_methods() {
    ClassDB::bind_method(D_METHOD("load_model", "gguf_path", "options"), &NvigiLLM::load_model, DEFVAL(Dictionary()));
    ClassDB::bind_method(D_METHOD("unload_model"), &NvigiLLM::unload_model);
    ClassDB::bind_method(D_METHOD("is_model_loaded"), &NvigiLLM::is_model_loaded);
    ClassDB::bind_method(D_METHOD("generate", "prompt", "system_prompt", "params"),
            &NvigiLLM::generate, DEFVAL(String()), DEFVAL(Dictionary()));
    ClassDB::bind_method(D_METHOD("generate_async", "prompt", "system_prompt", "params"),
            &NvigiLLM::generate_async, DEFVAL(String()), DEFVAL(Dictionary()));
    ClassDB::bind_method(D_METHOD("is_generating"), &NvigiLLM::is_generating);
    ClassDB::bind_method(D_METHOD("get_last_timings"), &NvigiLLM::get_last_timings);
    ClassDB::bind_method(D_METHOD("get_last_error"), &NvigiLLM::get_last_error);

    // Internal helpers used to marshal worker-thread results onto the main thread.
    ClassDB::bind_method(D_METHOD("_emit_token", "text"), &NvigiLLM::_emit_token_main);
    ClassDB::bind_method(D_METHOD("_finish_async", "full_text", "timings", "error"), &NvigiLLM::_finish_async_main);

    ADD_SIGNAL(MethodInfo("token", PropertyInfo(Variant::STRING, "text")));
    ADD_SIGNAL(MethodInfo("completed", PropertyInfo(Variant::STRING, "full_text"),
            PropertyInfo(Variant::DICTIONARY, "timings")));
}

NvigiLLM::NvigiLLM() {
}

NvigiLLM::~NvigiLLM() {
    unload_model();
}

int NvigiLLM::load_model(const String &gguf_path, const Dictionary &options) {
    auto &core = gdnvigi::CoreLoader::get();
    if (!core.is_initialized()) {
        last_error = "NVIGI is not initialized; call NvigiRuntime.initialize() first.";
        return (int)nvigi::kResultInvalidState;
    }
    if (instance) {
        unload_model();
    }

    const String backend = options.get("backend", "cuda");
    plugin_id = backend == String("cpu") ? nvigi::plugin::gpt::ggml::cpu::kId
                                         : nvigi::plugin::gpt::ggml::cuda::kId;

    const auto t0 = std::chrono::steady_clock::now();

    nvigi::Result result = core.load_interface(plugin_id, &igpt);
    if (result != nvigi::kResultOk) {
        last_error = String("Failed to load GPT plugin interface: ") + String(gdnvigi::CoreLoader::result_to_string(result));
        igpt = nullptr;
        return (int)result;
    }

    // Build an embedded model card that points NVIGI at the GGUF file.
    std::string path = to_utf8(globalized(gguf_path).replace("\\", "/"));
    std::string name = to_utf8(options.get("name", gguf_path.get_file()));
    const int64_t vram_mb = options.get("vram_mb", 2048);
    const int64_t n_layers = options.get("n_layers", 999);

    model_card_json = "{\n"
            "  \"name\": \"" + json_escape(name) + "\",\n"
            "  \"guid\": \"{00000000-0000-0000-0000-000000000001}\",\n"
            "  \"vram\": " + std::to_string(vram_mb) + ",\n"
            "  \"n_layers\": " + std::to_string(n_layers) + ",\n"
            "  \"local_files\": [\"" + json_escape(path) + "\"]";
    const String chat_template = options.get("chat_template", "");
    if (!chat_template.is_empty()) {
        model_card_json += ",\n  \"chat_template\": [\"" + json_escape(to_utf8(chat_template)) + "\"]";
    }
    model_card_json += "\n}";

    nvigi::GPTCreationParameters params{};
    params.contextSize = (int32_t)(int64_t)options.get("context_size", 4096);
    params.maxNumTokensToPredict = (int32_t)(int64_t)options.get("max_tokens", 512);
    params.flashAttention = (bool)options.get("flash_attention", false);

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

    result = igpt->createInstance(common, &instance);
    if (result != nvigi::kResultOk || !instance) {
        last_error = String("Failed to create GPT instance: ") + String(gdnvigi::CoreLoader::result_to_string(result));
        core.unload_interface(plugin_id, igpt);
        igpt = nullptr;
        instance = nullptr;
        return (int)result;
    }

    load_ms = ms_since(t0);
    last_error = String();
    return (int)nvigi::kResultOk;
}

void NvigiLLM::unload_model() {
    join_worker();
    if (instance && igpt) {
        igpt->destroyInstance(instance);
    }
    instance = nullptr;
    if (igpt) {
        gdnvigi::CoreLoader::get().unload_interface(plugin_id, igpt);
        igpt = nullptr;
    }
}

bool NvigiLLM::is_model_loaded() const {
    return instance != nullptr;
}

String NvigiLLM::generate(const String &prompt, const String &system_prompt, const Dictionary &params) {
    if (!instance) {
        last_error = "No model loaded.";
        return String();
    }
    if (generating.exchange(true)) {
        last_error = "A generation is already in progress.";
        return String();
    }
    join_worker();
    GenerationRun run;
    run.owner = this;
    run.async = false;
    nvigi::Result result = run_generation(run, to_utf8(prompt), to_utf8(system_prompt), params);
    generating.store(false);
    if (result != nvigi::kResultOk) {
        return String();
    }
    return String::utf8(run.full_text.c_str());
}

int NvigiLLM::generate_async(const String &prompt, const String &system_prompt, const Dictionary &params) {
    if (!instance) {
        last_error = "No model loaded.";
        return (int)nvigi::kResultInvalidState;
    }
    if (generating.exchange(true)) {
        last_error = "A generation is already in progress.";
        return (int)nvigi::kResultNotReady;
    }
    join_worker();

    std::string prompt_utf8 = to_utf8(prompt);
    std::string system_utf8 = to_utf8(system_prompt);
    worker = std::thread([this, prompt_utf8, system_utf8, params]() {
        GenerationRun run;
        run.owner = this;
        run.async = true;
        run_generation(run, prompt_utf8, system_utf8, params);
        generating.store(false);
    });
    return (int)nvigi::kResultOk;
}

bool NvigiLLM::is_generating() const {
    return generating.load();
}

nvigi::Result NvigiLLM::run_generation(GenerationRun &run, const std::string &prompt,
        const std::string &system_prompt, const Dictionary &params) {
    nvigi::InferenceDataTextSTLHelper user_text(prompt);
    nvigi::InferenceDataTextSTLHelper system_text(system_prompt);

    std::vector<nvigi::InferenceDataSlot> slots;
    if (!system_prompt.empty()) {
        slots.push_back({ nvigi::kGPTDataSlotSystem, system_text });
    }
    slots.push_back({ nvigi::kGPTDataSlotUser, user_text });
    nvigi::InferenceDataSlotArray inputs{ slots.size(), slots.data() };

    nvigi::GPTRuntimeParameters runtime{};
    runtime.temperature = (float)(double)params.get("temperature", 0.2);
    runtime.topP = (float)(double)params.get("top_p", 0.7);
    runtime.tokensToPredict = (int32_t)(int64_t)params.get("max_tokens", -1);
    runtime.interactive = (bool)params.get("interactive", false);
    const int64_t seed = params.get("seed", -1);
    if (seed >= 0) {
        runtime.seed = (uint32_t)seed;
    }

    nvigi::InferenceExecutionContext ctx{};
    ctx.instance = instance;
    ctx.inputs = &inputs;
    ctx.callback = &llm_inference_callback;
    ctx.callbackUserData = &run;
    ctx.runtimeParameters = runtime;

    run.start = std::chrono::steady_clock::now();
    nvigi::Result result = instance->evaluate(&ctx);
    const double total_ms = ms_since(run.start);

    Dictionary timings;
    timings["load_ms"] = load_ms;
    timings["ttft_ms"] = run.ttft_ms;
    timings["total_ms"] = total_ms;
    timings["tokens"] = run.tokens;
    const double gen_ms = run.ttft_ms >= 0.0 ? (total_ms - run.ttft_ms) : 0.0;
    timings["tokens_per_s"] = (run.tokens > 1 && gen_ms > 0.0) ? (double)(run.tokens - 1) / (gen_ms / 1000.0) : 0.0;

    String error;
    if (result != nvigi::kResultOk) {
        error = String("evaluate() failed: ") + String(gdnvigi::CoreLoader::result_to_string(result));
    } else if (run.final_state != nvigi::kInferenceExecutionStateDone) {
        error = "Inference did not reach the Done state.";
        result = nvigi::kResultInvalidState;
    }

    if (run.async) {
        call_deferred("_finish_async", String::utf8(run.full_text.c_str()), timings, error);
    } else {
        last_timings = timings;
        last_error = error;
    }
    return result;
}

void NvigiLLM::_emit_token_main(const String &text) {
    emit_signal("token", text);
}

void NvigiLLM::_finish_async_main(const String &full_text, const Dictionary &timings, const String &error) {
    last_timings = timings;
    last_error = error;
    emit_signal("completed", full_text, timings);
}

void NvigiLLM::join_worker() {
    if (worker.joinable()) {
        worker.join();
    }
}

Dictionary NvigiLLM::get_last_timings() const {
    return last_timings;
}

String NvigiLLM::get_last_error() const {
    return last_error;
}
