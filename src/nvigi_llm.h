// SPDX-FileCopyrightText: Copyright (c) 2026 godot-nvigi contributors
// SPDX-License-Identifier: MIT

#ifndef GDNVIGI_LLM_H
#define GDNVIGI_LLM_H

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/dictionary.hpp>

#include <atomic>
#include <string>
#include <thread>

#include <nvigi.h>
#include <nvigi_ai.h>
#include <nvigi_gpt.h>

namespace godot {

// Local LLM inference through the NVIGI GGML GPT plugin (CUDA or CPU).
//
// Typical use:
//   var llm := NvigiLLM.new()
//   llm.load_model("C:/models/qwen2.5-0.5b-instruct-q8_0.gguf", {"backend": "cuda"})
//   var reply := llm.generate("Say hi", "You are a pirate.")
//   print(llm.get_last_timings())
class NvigiLLM : public RefCounted {
    GDCLASS(NvigiLLM, RefCounted)

protected:
    static void _bind_methods();

public:
    NvigiLLM();
    ~NvigiLLM();

    // Loads a GGUF model file. NvigiRuntime.initialize() must have been
    // called first. Options (all optional):
    //   backend (String)        - "cuda" (default) or "cpu"
    //   context_size (int)      - default 4096
    //   max_tokens (int)        - default creation-time max prediction, 512
    //   vram_budget_mb (int)    - default 8192
    //   vram_mb (int)           - VRAM the model claims to need, default 2048
    //   n_layers (int)          - transformer layer count of the model,
    //                             used for GPU offload ratio; default 999
    //                             (meaning "offload everything")
    //   threads (int)           - CPU threads, default 4
    //   name (String)           - display name in logs
    //   chat_template (String)  - optional Jinja-style template override
    // Returns an nvigi result code (0 == OK).
    int load_model(const String &gguf_path, const Dictionary &options = Dictionary());
    void unload_model();
    bool is_model_loaded() const;

    // Blocking single-turn generation. Returns the reply text ("" on error;
    // see get_last_error()). params (all optional):
    //   temperature (float) - default 0.2
    //   top_p (float)       - default 0.7
    //   max_tokens (int)    - new tokens to predict, default -1 (use creation value)
    //   seed (int)          - RNG seed
    //   interactive (bool)  - keep chat KV-cache between calls, default false
    String generate(const String &prompt, const String &system_prompt = String(), const Dictionary &params = Dictionary());

    // Non-blocking variant; runs on an internal thread and emits:
    //   token(text: String)                          - per generated chunk
    //   completed(full_text: String, timings: Dictionary)
    // Returns an nvigi result code (0 == accepted). Only one generation may
    // be in flight per NvigiLLM instance.
    int generate_async(const String &prompt, const String &system_prompt = String(), const Dictionary &params = Dictionary());

    bool is_generating() const;

    // Timings of the last completed generate/generate_async call:
    //   { load_ms, ttft_ms, total_ms, tokens, tokens_per_s }
    Dictionary get_last_timings() const;
    String get_last_error() const;

    struct GenerationRun;

private:
    nvigi::Result run_generation(GenerationRun &run, const std::string &prompt,
            const std::string &system_prompt, const Dictionary &params);
    void _emit_token_main(const String &text);
    void _finish_async_main(const String &full_text, const Dictionary &timings, const String &error);
    void join_worker();


    nvigi::IGeneralPurposeTransformer *igpt = nullptr;
    nvigi::InferenceInstance *instance = nullptr;
    nvigi::PluginID plugin_id{};

    // Storage that must stay alive while the instance exists.
    std::string model_card_json;

    std::atomic<bool> generating{ false };
    std::thread worker;

    double load_ms = 0.0;
    Dictionary last_timings;
    String last_error;
};

} // namespace godot

#endif // GDNVIGI_LLM_H
