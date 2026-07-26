# godot-nvigi

**Local, on-device AI inference for Godot 4 via NVIDIA In-Game Inferencing (NVIGI).**

A Windows x64 GDExtension that wraps the [NVIGI SDK](https://github.com/NVIDIA-RTX/NVIGI)
so GDScript can run **LLM text generation** (GGUF models through llama.cpp/CUDA) and
**Whisper speech-to-text** entirely on the player's GPU — no cloud, no Python, no server
process.

> **Not affiliated with or endorsed by NVIDIA.** This is an independent open-source
> wrapper around NVIDIA's public NVIGI SDK. NVIDIA also maintains its own official
> [nvigi_godot_extension](https://github.com/NVIDIA-RTX/nvigi_godot_extension) with
> auto-generated bindings and a broader feature set; this project is a small,
> hand-written, MIT-licensed integration focused on a minimal, readable core
> (three classes, one build command) that is easy to audit and extend.

## Architecture

```
 GDScript (demo/main.gd, your game)
    |
    |  NvigiRuntime / NvigiLLM / NvigiASR   (Godot RefCounted classes)
    v
 gdnvigi.dll  (this repo: src/*.cpp, built with SCons + godot-cpp)
    |
    |  LoadLibrary + nvigiInit/nvigiLoadInterface   (no import lib needed)
    v
 nvigi.core.framework.dll  (NVIGI core, from the NVIGI-Core PDK release)
    |
    +-- nvigi.plugin.gpt.ggml.cuda.dll   --> llama.cpp (CUDA)  --> your GGUF model
    +-- nvigi.plugin.gpt.ggml.cpu.dll    --> llama.cpp (CPU)
    +-- nvigi.plugin.asr.ggml.cuda.dll   --> whisper.cpp (CUDA) --> ggml whisper model
    +-- nvigi.plugin.asr.ggml.cpu.dll    --> whisper.cpp (CPU)
    +-- nvigi.plugin.hwi.cuda.dll        (CUDA hardware interface)
```

Models are loaded through NVIGI's *embedded model card* mechanism, so you can point
`load_model()` at **any GGUF file on disk** — no NVIGI model-repository directory
layout required.

## Requirements

- Windows 10/11 x64
- NVIDIA RTX GPU (tested: RTX 5060 Ti / Blackwell, driver 595.79) for the CUDA
  backends; the CPU backends work anywhere with AVX2
- Godot **4.5+** (editor binary is enough)
- Visual Studio 2022 (MSVC v143 **14.43+** — required to link NVIGI's prebuilt GGML
  libraries), Python 3.8+, `pip install scons`, git

## Quickstart

```powershell
git clone --recursive https://github.com/<you>/godot-nvigi
cd godot-nvigi

# 1. NVIGI SDK: downloads the prebuilt NVIGI-Core PDK, pulls NVIGI's prebuilt
#    llama.cpp/whisper.cpp/CUDA dependencies (~4 GB, cached by packman), builds
#    the four GGML plugin DLLs, stages everything into demo/addons/godot_nvigi/bin
.\setup.ps1 -Models          # -Models also fetches the two small test models

# 2. Build the GDExtension (the one documented command)
scons target=template_release

# 3. Run the demo (or open demo/ in the Godot editor)
godot --path demo

# Headless smoke test (LLM + ASR on GPU, prints timings, exit code 0/1):
godot --headless --path demo -s smoke.gd
# CPU-only variant:
godot --headless --path demo -s smoke.gd -- --cpu
```

`setup.ps1` implements the **PDK path** (recommended: no NVIGI Core source build).
If you prefer building NVIGI Core from source, run
`thirdparty/nvigi/01_build_pack_core.bat` from a VS2022 Developer Command Prompt
and point the `nvigi_core` junction at `thirdparty/nvigi/NVIGI-Core` instead —
see [NVIGI's README](https://github.com/NVIDIA-RTX/NVIGI) for details.

## API

All three classes are regular `RefCounted` — create them with `.new()`.
Integer return codes are NVIGI result codes (`0` == OK); decode with
`NvigiRuntime.result_to_string(code)`.

### NvigiRuntime

| Member | Description |
| --- | --- |
| `initialize(sdk_dir: String, log_level := 1) -> int` | Loads `nvigi.core.framework.dll` from `sdk_dir` (accepts `res://` paths) and initializes NVIGI with `sdk_dir` as the plugin search path. `log_level`: 0 off, 1 default, 2 verbose. |
| `shutdown() -> int` | Tears the framework down (also called automatically on extension unload). |
| `is_initialized() -> bool` | |
| `get_plugins() -> Array[Dictionary]` | One dict per detected plugin: `{ id: "{GUID}", crc24, name, version, api_version, supported, status }`. |
| `get_adapters() -> Array[Dictionary]` | `{ vendor, vendor_id, dedicated_memory_mb, driver_version, architecture }`. |
| `result_to_string(code: int) -> String` *(static)* | Human-readable NVIGI result code. |

### NvigiLLM

| Member | Description |
| --- | --- |
| `load_model(gguf_path, options := {}) -> int` | Loads any GGUF file. Options: `backend` ("cuda"/"cpu"), `context_size` (4096), `max_tokens` (512), `vram_budget_mb` (8192), `vram_mb` (2048 — VRAM the model needs), `n_layers` (999 — model layer count, used for the GPU offload ratio), `threads` (4), `name`, `chat_template`. |
| `generate(prompt, system_prompt := "", params := {}) -> String` | Blocking single-turn generation. Params: `temperature` (0.2), `top_p` (0.7), `max_tokens` (-1), `seed`, `interactive` (false — set true to keep the chat KV-cache between calls). Empty string on error (`get_last_error()`). |
| `generate_async(prompt, system_prompt := "", params := {}) -> int` | Same, on an internal thread. Emits `token(text)` per chunk and `completed(full_text, timings)`. |
| `signal token(text: String)` | Streaming chunk (usually one token). |
| `signal completed(full_text: String, timings: Dictionary)` | |
| `get_last_timings() -> Dictionary` | `{ load_ms, ttft_ms, total_ms, tokens, tokens_per_s }`. |
| `is_generating() / is_model_loaded() / unload_model() / get_last_error()` | |

### NvigiASR

| Member | Description |
| --- | --- |
| `load_model(model_path, options := {}) -> int` | Loads a whisper.cpp GGML model. **The file must have a `.gguf` extension** (rename `ggml-*.bin` if needed). Options: `backend`, `language` ("en"), `threads`, `vram_budget_mb`, `vram_mb`. |
| `transcribe_wav(wav_path) -> Dictionary` | Blocking; PCM16 WAV of any rate/channels (auto downmix + resample to 16 kHz mono). Returns `{ ok, text, ms, error }`. |
| `is_model_loaded() / unload_model() / get_last_error()` | |

### Adding more plugins (TTS, embeddings, ...)

The pattern is deliberately uniform — see `src/nvigi_llm.cpp`:

1. Get the interface: `CoreLoader::get().load_interface(plugin_id, &iface)`.
2. Build creation params, chain `CommonCreationParameters` (+ an embedded model
   card JSON + `FileIOCallbacks` from `nvigi_file_io.h` for arbitrary file paths).
3. `iface->createInstance(...)`, then per call fill an
   `InferenceExecutionContext` with input slots and a callback, and `evaluate()`.
4. Release with `destroyInstance` + `unload_interface`.

## Test models used by the demo / smoke test

| Model | File | License |
| --- | --- | --- |
| [Qwen2.5-0.5B-Instruct-GGUF](https://huggingface.co/Qwen/Qwen2.5-0.5B-Instruct-GGUF) | `qwen2.5-0.5b-instruct-q4_k_m.gguf` (~490 MB) | Apache-2.0 |
| [whisper.cpp tiny.en](https://huggingface.co/ggerganov/whisper.cpp) | `ggml-tiny.en.gguf` (~78 MB; whisper.cpp's `ggml-tiny.en.bin`, renamed - the NVIGI ASR plugin requires the `.gguf` extension) | MIT |

Any llama.cpp-compatible GGUF works with `NvigiLLM` (subject to VRAM). Models from
NVIDIA's own catalog (downloaded through the NVIGI Developer Pack) are covered by the
**NVIDIA Community Model License** — download them yourself; never commit them.

## License matrix

This repo is careful about what is (and is not) redistributed:

| Component | Where | License | In git? |
| --- | --- | --- | --- |
| Wrapper code, demo, scripts (this repo) | `src/`, `demo/`, `setup.ps1` | **MIT** | yes |
| NVIGI Core + Plugins source | `thirdparty/nvigi` (submodule) | MIT (NVIDIA) | submodule ref only |
| godot-cpp | `thirdparty/godot-cpp` (submodule) | MIT | submodule ref only |
| NVIGI Core PDK binaries | `thirdparty/_sdk/` (downloaded) | MIT + NVIDIA/third-party notices in pack | **no** (gitignored) |
| CUDA redistributables (cublas, nvrtc, cudart…) | pulled by packman / staged to `demo/addons/godot_nvigi/bin` | NVIDIA CUDA EULA (redistributable set) | **no** |
| Microsoft DXC / Agility SDK DLLs | only needed for NVIGI's experimental D3D12 backends (not used here) | Microsoft terms — **fetched by setup scripts, never committed** | **no** |
| AI models (GGUF, whisper, NVIDIA catalog) | `demo/models/` (user-downloaded) | Apache-2.0 / MIT / NVIDIA Community Model License respectively | **no** |
| `demo/sample.wav` | generated locally with Windows SAPI for this repo | CC0 / public domain | yes |

## CI

`/.github/workflows/build.yml` builds the wrapper DLL on `windows-latest` from the
submodule headers alone (no PDK download needed at compile time, since NVIGI is
loaded dynamically at runtime). **Model inference is not exercised in CI** — GitHub
runners have no NVIDIA GPU. Run `demo/smoke.gd` locally for the real end-to-end test.

## Known limitations (v0.1)

- Windows x64 only (NVIGI's platform support).
- `NvigiLLM.generate*` is single-turn by default; pass `{"interactive": true}` for
  chat KV-cache reuse, but there is no structured multi-turn history API yet.
- Whisper streaming ("partial" callbacks) is not exposed; `transcribe_wav` is
  one-shot.
- One generation in flight per `NvigiLLM` instance.
- Token signals may split multi-byte UTF-8 sequences on token boundaries (rare;
  the final `completed` text is always correct).
- NVIGI's experimental D3D12/Vulkan GGML backends are not wrapped.

## Roadmap

- **TTS**: wrap `nvigi.plugin.tts.asqflow-ggml` (`NvigiTTS.speak(text) -> AudioStreamWAV`).
- **Embeddings**: `nvigi.plugin.embed.ggml` for RAG-style lookups in-game.
- **CUDA-in-Graphics scheduling**: chain `D3D12Parameters` from Godot's D3D12
  rendering device so inference and rendering share the GPU politely (CiG).
- **Chat history API** on `NvigiLLM` (message list in, templated prompt out).
- **Linux** once NVIGI's Linux packaging stabilizes.

## Acknowledgements

Built on [NVIDIA In-Game Inferencing](https://developer.nvidia.com/rtx/in-game-inferencing),
[llama.cpp](https://github.com/ggml-org/llama.cpp),
[whisper.cpp](https://github.com/ggml-org/whisper.cpp) and
[godot-cpp](https://github.com/godotengine/godot-cpp).
NVIDIA, CUDA, RTX and In-Game Inferencing are trademarks of NVIDIA Corporation.
