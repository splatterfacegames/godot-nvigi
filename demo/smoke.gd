# SPDX-FileCopyrightText: Copyright (c) 2026 godot-nvigi contributors
# SPDX-License-Identifier: MIT
#
# Headless smoke test for godot-nvigi. Run with:
#
#   godot --headless --path demo -s smoke.gd
#
# Requires (see setup.ps1 / README):
#   demo/addons/godot_nvigi/bin/       - wrapper + NVIGI DLLs
#   demo/models/qwen2.5-0.5b-instruct-q4_k_m.gguf
#   demo/models/ggml-tiny.en.bin
#
# Exits with code 0 on success, 1 on failure.
extends SceneTree

const LLM_MODEL := "res://models/qwen2.5-0.5b-instruct-q4_k_m.gguf"
const ASR_MODEL := "res://models/ggml-tiny.en.bin"

func _init() -> void:
	var failures := 0

	print("=== godot-nvigi smoke test ===")

	# --- Runtime -----------------------------------------------------------
	var runtime := NvigiRuntime.new()
	var rc := runtime.initialize("res://addons/godot_nvigi/bin", 1)
	if rc != 0:
		printerr("NvigiRuntime.initialize failed: ", NvigiRuntime.result_to_string(rc))
		quit(1)
		return
	print("nvigi initialized")

	for adapter in runtime.get_adapters():
		print("adapter: ", adapter)
	var plugins := runtime.get_plugins()
	print("plugins detected: ", plugins.size())
	for plugin in plugins:
		print("  - %s %s supported=%s" % [plugin.name, plugin.version, plugin.supported])

	# --- LLM ---------------------------------------------------------------
	var backend := "cuda"
	for arg in OS.get_cmdline_user_args():
		if arg == "--cpu":
			backend = "cpu"
	print("using backend: ", backend)

	var llm := NvigiLLM.new()
	rc = llm.load_model(LLM_MODEL, {"backend": backend, "n_layers": 24, "vram_mb": 1024, "context_size": 2048})
	if rc != 0:
		printerr("NvigiLLM.load_model failed: ", NvigiRuntime.result_to_string(rc), " / ", llm.get_last_error())
		failures += 1
	else:
		var reply := llm.generate(
			"Reply with exactly one short sentence: what is the capital of France?",
			"You are a concise assistant.",
			{"temperature": 0.0, "max_tokens": 48})
		var timings := llm.get_last_timings()
		if reply.is_empty():
			printerr("LLM generate failed: ", llm.get_last_error())
			failures += 1
		else:
			print("LLM reply: ", reply.strip_edges())
			print("LLM timings: load_ms=%.0f ttft_ms=%.0f total_ms=%.0f tokens=%d tokens_per_s=%.1f" % [
				timings.get("load_ms", -1.0), timings.get("ttft_ms", -1.0),
				timings.get("total_ms", -1.0), timings.get("tokens", -1),
				timings.get("tokens_per_s", -1.0)])
		llm.unload_model()

	# --- ASR ---------------------------------------------------------------
	var asr := NvigiASR.new()
	rc = asr.load_model(ASR_MODEL, {"backend": backend})
	if rc != 0:
		printerr("NvigiASR.load_model failed: ", NvigiRuntime.result_to_string(rc), " / ", asr.get_last_error())
		failures += 1
	else:
		var result := asr.transcribe_wav("res://sample.wav")
		if result.ok:
			print("ASR text: ", String(result.text).strip_edges())
			print("ASR ms: %.0f" % result.ms)
		else:
			printerr("ASR failed: ", result.error)
			failures += 1
		asr.unload_model()

	runtime.shutdown()

	if failures > 0:
		printerr("smoke test FAILED (%d failures)" % failures)
		quit(1)
	else:
		print("smoke test PASSED")
		quit(0)
