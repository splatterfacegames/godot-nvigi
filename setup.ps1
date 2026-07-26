# SPDX-FileCopyrightText: Copyright (c) 2026 godot-nvigi contributors
# SPDX-License-Identifier: MIT
#
# One-shot setup for godot-nvigi on Windows x64.
#
#   .\setup.ps1            # submodules + NVIGI PDK + plugin build + stage DLLs
#   .\setup.ps1 -Models    # ...and download the small test models (see notes)
#
# What it does:
#   1. Initializes git submodules (NVIGI + godot-cpp).
#   2. Downloads the prebuilt NVIGI Core PDK from the NVIGI-Core GitHub release
#      (this avoids building NVIGI Core from source).
#   3. Links the PDK into NVIGI-Plugins as `nvigi_core` and runs NVIGI's own
#      setup (packman pulls prebuilt llama.cpp/whisper.cpp/CUDA dependencies).
#   4. Builds the four GGML plugin DLLs (GPT/ASR x CUDA/CPU) with MSBuild.
#   5. Stages all runtime DLLs into demo/addons/godot_nvigi/bin/.
#
# After this, build the GDExtension itself with:  scons target=template_release
#
# NOTE ON LICENSES: the NVIGI PDK and the packman-pulled dependencies include
# NVIDIA- and Microsoft-licensed binaries (e.g. CUDA redistributables). They
# are downloaded to your machine only and are gitignored - do not commit them.
# Models downloaded via -Models are under their own licenses (Apache-2.0 for
# Qwen2.5, MIT for whisper.cpp models).

param(
    [switch]$Models,
    [string]$NvigiCoreVersion = "1.6.1"
)

$ErrorActionPreference = "Stop"
$root = $PSScriptRoot

function Step($msg) { Write-Host "==> $msg" -ForegroundColor Cyan }

# --- 1. submodules ----------------------------------------------------------
Step "Initializing git submodules"
git -C $root submodule update --init --recursive
if ($LASTEXITCODE -ne 0) { throw "git submodule update failed" }

# --- 2. NVIGI Core PDK ------------------------------------------------------
$sdkDir = Join-Path $root "thirdparty\_sdk"
$pdkDir = Join-Path $sdkDir "nvigi_core_pdk"
if (-not (Test-Path (Join-Path $pdkDir "bin\x64\Release\nvigi.core.framework.dll"))) {
    Step "Downloading NVIGI Core PDK $NvigiCoreVersion"
    New-Item -ItemType Directory -Force -Path $sdkDir | Out-Null
    $pdk7z = Join-Path $sdkDir "nvigi_core_pdk.7z"
    $url = "https://github.com/NVIDIA-RTX/NVIGI-Core/releases/download/ga-$NvigiCoreVersion/nvigi_core_pdk_win64_$NvigiCoreVersion.7z"
    Invoke-WebRequest -Uri $url -OutFile $pdk7z

    # Find or fetch a 7z extractor (the PDK ships as .7z).
    $sevenZip = Get-Command 7z -ErrorAction SilentlyContinue
    if ($sevenZip) { $sevenZip = $sevenZip.Source }
    elseif (Test-Path "$env:ProgramFiles\7-Zip\7z.exe") { $sevenZip = "$env:ProgramFiles\7-Zip\7z.exe" }
    else {
        Step "7-Zip not found; downloading standalone 7zr.exe"
        $sevenZip = Join-Path $sdkDir "7zr.exe"
        Invoke-WebRequest -Uri "https://www.7-zip.org/a/7zr.exe" -OutFile $sevenZip
    }
    & $sevenZip x -y "-o$pdkDir" $pdk7z | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "PDK extraction failed" }
    Remove-Item $pdk7z
} else {
    Step "NVIGI Core PDK already present"
}

# --- 3. link PDK + NVIGI-Plugins setup -------------------------------------
$plugins = Join-Path $root "thirdparty\nvigi\NVIGI-Plugins"
$coreLink = Join-Path $plugins "nvigi_core"
if (-not (Test-Path $coreLink)) {
    Step "Creating nvigi_core junction"
    cmd /c mklink /j "$coreLink" "$pdkDir" | Out-Null
}

Step "Running NVIGI-Plugins setup (packman + premake; first run downloads ~4 GB)"
Push-Location $plugins
cmd /c "call .\setup.bat vs2022"
if ($LASTEXITCODE -ne 0) { Pop-Location; throw "NVIGI-Plugins setup.bat failed" }
Pop-Location

# --- 4. build plugin DLLs ---------------------------------------------------
Step "Locating MSBuild"
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vsPath = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath
if (-not $vsPath) { throw "Visual Studio 2022 with MSBuild not found" }
$msbuild = Join-Path $vsPath "MSBuild\Current\Bin\MSBuild.exe"

Step "Building NVIGI GPT + ASR plugins (Release x64)"
$projects = @(
    "nvigi.plugin.gpt.ggml.cuda.vcxproj",
    "nvigi.plugin.gpt.ggml.cpu.vcxproj",
    "nvigi.plugin.asr.ggml.cuda.vcxproj",
    "nvigi.plugin.asr.ggml.cpu.vcxproj"
)
foreach ($proj in $projects) {
    & $msbuild (Join-Path $plugins "_project\vs2022\$proj") /m /v:m /p:Configuration=Release /p:Platform=x64
    if ($LASTEXITCODE -ne 0) { throw "Build failed: $proj" }
}

# --- 5. stage runtime DLLs --------------------------------------------------
Step "Staging runtime DLLs into demo/addons/godot_nvigi/bin"
$bin = Join-Path $root "demo\addons\godot_nvigi\bin"
New-Item -ItemType Directory -Force -Path $bin | Out-Null

$pdkBin = Join-Path $pdkDir "bin\x64\Release"
@(
    "nvigi.core.framework.dll", "nvigi.plugin.hwi.common.dll", "nvigi.plugin.hwi.cuda.dll",
    "cig_scheduler_settings.dll", "cudart64_12.dll", "zlib1.dll", "cupti64_2025.1.0.dll"
) | ForEach-Object { Copy-Item (Join-Path $pdkBin $_) $bin -Force }

@("gpt.ggml.cuda", "gpt.ggml.cpu", "asr.ggml.cuda", "asr.ggml.cpu") | ForEach-Object {
    Copy-Item (Join-Path $plugins "_artifacts\nvigi.plugin.$_\x64\Release\nvigi.plugin.$_.dll") $bin -Force
}

$cudaBin = Join-Path $plugins "external\cuda\bin"
@("cublas64_12.dll", "cublasLt64_12.dll", "nvrtc64_120_0.dll", "nvrtc-builtins64_128.dll") |
    ForEach-Object { Copy-Item (Join-Path $cudaBin $_) $bin -Force }
Copy-Item (Join-Path $cudaBin "nvrtc-builtins64_128.dll") (Join-Path $bin "nvrtc-builtins.alt64_128.dll") -Force

# --- 6. optional test models ------------------------------------------------
if ($Models) {
    Step "Downloading test models (Qwen2.5-0.5B-Instruct GGUF, whisper tiny.en)"
    $modelsDir = Join-Path $root "demo\models"
    New-Item -ItemType Directory -Force -Path $modelsDir | Out-Null
    $qwen = Join-Path $modelsDir "qwen2.5-0.5b-instruct-q4_k_m.gguf"
    if (-not (Test-Path $qwen)) {
        Invoke-WebRequest -Uri "https://huggingface.co/Qwen/Qwen2.5-0.5B-Instruct-GGUF/resolve/main/qwen2.5-0.5b-instruct-q4_k_m.gguf" -OutFile $qwen
    }
    $whisper = Join-Path $modelsDir "ggml-tiny.en.gguf"
    if (-not (Test-Path $whisper)) {
        Invoke-WebRequest -Uri "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-tiny.en.bin" -OutFile $whisper
    }
}

Step "Done. Next: 'scons target=template_release' then open demo/ in Godot 4.5+"
