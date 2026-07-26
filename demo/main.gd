# SPDX-FileCopyrightText: Copyright (c) 2026 godot-nvigi contributors
# SPDX-License-Identifier: MIT
extends Control

const LLM_MODEL := "res://models/qwen2.5-0.5b-instruct-q4_k_m.gguf"
const ASR_MODEL := "res://models/ggml-tiny.en.bin"

@onready var prompt_edit: LineEdit = %PromptEdit
@onready var generate_button: Button = %GenerateButton
@onready var reply_label: RichTextLabel = %ReplyLabel
@onready var transcribe_button: Button = %TranscribeButton
@onready var transcript_label: Label = %TranscriptLabel
@onready var latency_label: Label = %LatencyLabel
@onready var status_label: Label = %StatusLabel

var runtime := NvigiRuntime.new()
var llm := NvigiLLM.new()
var asr := NvigiASR.new()
var llm_ready := false
var asr_ready := false

func _ready() -> void:
	generate_button.pressed.connect(_on_generate_pressed)
	transcribe_button.pressed.connect(_on_transcribe_pressed)
	prompt_edit.text_submitted.connect(func(_t: String) -> void: _on_generate_pressed())
	llm.token.connect(_on_llm_token)
	llm.completed.connect(_on_llm_completed)

	var rc := runtime.initialize("res://addons/godot_nvigi/bin", 1)
	if rc != 0:
		status_label.text = "NVIGI init failed: %s" % NvigiRuntime.result_to_string(rc)
		return
	var gpus := runtime.get_adapters().map(func(a: Dictionary) -> String: return "%s (%d MB)" % [a.vendor, a.dedicated_memory_mb])
	status_label.text = "NVIGI initialized. %d plugins. GPU: %s. Loading models..." % [
		runtime.get_plugins().size(), ", ".join(gpus)]

	# Load models off the main thread so the UI stays responsive.
	WorkerThreadPool.add_task(_load_models)

func _load_models() -> void:
	var llm_rc := llm.load_model(LLM_MODEL, {"backend": "cuda", "n_layers": 24, "vram_mb": 1024, "context_size": 2048})
	var asr_rc := asr.load_model(ASR_MODEL, {"backend": "cuda"})
	call_deferred("_models_loaded", llm_rc, asr_rc)

func _models_loaded(llm_rc: int, asr_rc: int) -> void:
	llm_ready = llm_rc == 0
	asr_ready = asr_rc == 0
	var parts := PackedStringArray()
	parts.append("LLM: %s" % ("ready" if llm_ready else NvigiRuntime.result_to_string(llm_rc)))
	parts.append("ASR: %s" % ("ready" if asr_ready else NvigiRuntime.result_to_string(asr_rc)))
	status_label.text = " | ".join(parts)
	generate_button.disabled = not llm_ready
	transcribe_button.disabled = not asr_ready

func _on_generate_pressed() -> void:
	if not llm_ready or llm.is_generating():
		return
	reply_label.text = ""
	generate_button.disabled = true
	var rc := llm.generate_async(prompt_edit.text, "You are a helpful, concise assistant.",
			{"temperature": 0.2, "max_tokens": 256})
	if rc != 0:
		status_label.text = "generate_async failed: %s" % NvigiRuntime.result_to_string(rc)
		generate_button.disabled = false

func _on_llm_token(text: String) -> void:
	reply_label.text += text

func _on_llm_completed(full_text: String, timings: Dictionary) -> void:
	reply_label.text = full_text
	generate_button.disabled = false
	latency_label.text = "load %.0f ms | first token %.0f ms | total %.0f ms | %d tokens | %.1f tok/s" % [
		timings.get("load_ms", -1.0), timings.get("ttft_ms", -1.0),
		timings.get("total_ms", -1.0), timings.get("tokens", 0), timings.get("tokens_per_s", 0.0)]

func _on_transcribe_pressed() -> void:
	if not asr_ready:
		return
	transcribe_button.disabled = true
	transcript_label.text = "Transcribing..."
	WorkerThreadPool.add_task(func() -> void:
		var result := asr.transcribe_wav("res://sample.wav")
		call_deferred("_transcribe_done", result))

func _transcribe_done(result: Dictionary) -> void:
	transcribe_button.disabled = false
	if result.ok:
		transcript_label.text = "\"%s\"  (%.0f ms)" % [String(result.text).strip_edges(), result.ms]
	else:
		transcript_label.text = "ASR error: %s" % result.error
