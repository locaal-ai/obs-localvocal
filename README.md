# LocalVocal - Speech AI assistant OBS Plugin

<div align="center">

[![GitHub](https://img.shields.io/github/license/occ-ai/obs-localvocal)](https://github.com/occ-ai/obs-localvocal/blob/main/LICENSE)
[![GitHub Workflow Status](https://img.shields.io/github/actions/workflow/status/occ-ai/obs-localvocal/push.yaml)](https://github.com/occ-ai/obs-localvocal/actions/workflows/push.yaml)
[![Total downloads](https://img.shields.io/github/downloads/occ-ai/obs-localvocal/total)](https://github.com/occ-ai/obs-localvocal/releases)
[![GitHub release (latest by date)](https://img.shields.io/github/v/release/occ-ai/obs-localvocal)](https://github.com/occ-ai/obs-localvocal/releases)

</div>

## Introduction

LocalVocal live-streaming AI assistant plugin allows you to transcribe, locally on your machine, audio speech into text and perform various language processing functions on the text using AI / LLMs (Large Language Models). ✅ No GPU required, ✅ no cloud costs, ✅ no network and ✅ no downtime! Privacy first - all data stays on your machine.

If this free plugin has been valuable to you consider adding a ⭐ to this GH repo, rating it [on OBS](https://obsproject.com/forum/resources/localvocal-live-stream-ai-assistant.1769/), subscribing to [my YouTube channel](https://www.youtube.com/@royshilk) where I post updates, and supporting my work: https://github.com/sponsors/royshil

<div align="center">
  <a href="https://youtu.be/5XqTMqpui3Q" target="_blank">
    <img width="40%" src="https://github-production-user-asset-6210df.s3.amazonaws.com/441170/267728411-334551b8-6a7f-42bf-8434-6ad6b512a401.jpeg" />
  </a>
  <a href="https://youtu.be/Q34LQsx-nlg" target="_blank">
    <img width="40%" src="https://github-production-user-asset-6210df.s3.amazonaws.com/441170/271725640-3e5edd4a-9d07-4d19-b631-c70f91c73c27.PNG" />
  </a><br/>
  https://youtu.be/5XqTMqpui3Q & https://youtu.be/Q34LQsx-nlg
</div>

Current Features:
- Transcribe audio to text in real time in 100 languages
- Display captions on screen using text sources
- Send captions to a file (which can be read by external sources)
- Send captions on a RTMP stream to e.g. YouTube, Twitch
- Bring your own Whisper model (GGML)
- Translate captions in real time to major languages

Roadmap:
- Remove unwanted words from the transcription
- Summarize the text and show "highlights" on screen
- Detect key moments in the stream and allow triggering events (like replay)
- Detect emotions/sentiment and allow triggering events (like changing the scene or colors etc.)

Internally the plugin is running a neural network ([OpenAI Whisper](https://github.com/openai/whisper)) locally to predict in real time the speech and provide captions.

It's using the [Whisper.cpp](https://github.com/ggerganov/whisper.cpp) project from [ggerganov](https://github.com/ggerganov) to run the Whisper network in a very efficient way on CPUs and GPUs.

Check out our other plugins:
- [Background Removal](https://github.com/occ-ai/obs-backgroundremoval) removes background from webcam without a green screen.
- 🚧 Experimental 🚧 [CleanStream](https://github.com/occ-ai/obs-cleanstream) for real-time filler word (uh,um) and profanity removal from live audio stream
- [URL/API Source](https://github.com/occ-ai/obs-urlsource) that allows fetching live data from an API and displaying it in OBS.
- [Polyglot](https://github.com/occ-ai/obs-polyglot) translation AI plugin for real-time, local translation to hunderds of languages

## Download
Check out the [latest releases](https://github.com/occ-ai/obs-localvocal/releases) for downloads and install instructions.

## Building

The plugin was built and tested on Mac OSX  (Intel & Apple silicon), Windows and Linux.

Start by cloning this repo to a directory of your choice.

Remember to sync and fetch the submodules before building, e.g.
```sh
$ git submodule sync --recursive
$ git update --init --recursive
```

### Mac OSX

Using the CI pipeline scripts, locally you would just call the zsh script. By default this builds a universal binary for both Intel and Apple Silicon. To build for a specific architecture please see `.github/scripts/.build.zsh` for the `-arch` options.

```sh
$ ./.github/scripts/build-macos -c Release
```

#### Install
The above script should succeed and the plugin files (e.g. `obs-urlsource.plugin`) will reside in the `./release/Release` folder off of the root. Copy the `.plugin` file to the OBS directory e.g. `~/Library/Application Support/obs-studio/plugins`.

To get `.pkg` installer file, run for example
```sh
$ ./.github/scripts/package-macos -c Release
```
(Note that maybe the outputs will be in the `Release` folder and not the `install` folder like `pakage-macos` expects, so you will need to rename the folder from `build_x86_64/Release` to `build_x86_64/install`)

### Linux (Ubuntu)

Use the CI scripts again
```sh
$ ./.github/scripts/build-linux.sh
```

Copy the results to the standard OBS folders on Ubuntu
```sh
$ sudo cp -R release/RelWithDebInfo/lib/* /usr/lib/x86_64-linux-gnu/
$ sudo cp -R release/RelWithDebInfo/share/* /usr/share/
```
Note: The official [OBS plugins guide](https://obsproject.com/kb/plugins-guide) recommends adding plugins to the `~/.config/obs-studio/plugins` folder.

### Windows

Use the CI scripts again, for example:

```powershell
> .github/scripts/Build-Windows.ps1 -Target x64 -CMakeGenerator "Visual Studio 17 2022"
```

The build should exist in the `./release` folder off the root. You can manually install the files in the OBS directory.

#### Building with CUDA support on Windows

To build with CUDA support on Windows, you need to install the CUDA toolkit from NVIDIA. The CUDA toolkit is available for download from [here](https://developer.nvidia.com/cuda-downloads).

After installing the CUDA toolkit, you need to set variables to point CMake to the CUDA toolkit installation directory. For example, if you have installed the CUDA toolkit in `C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v11.4`, you need to set `CUDA_TOOLKIT_ROOT_DIR` to `C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v11.4` and `LOCALVOCAL_WITH_CUDA` to `ON` when running `.github/scripts/Build-Windows.ps1`.

For example
```powershell
.github/scripts/Build-Windows.ps1 -Target x64 -ExtraCmakeArgs '-D','LOCALVOCAL_WITH_CUDA=ON','-D',"CUDA_TOOLKIT_ROOT_DIR='C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v12.2'"
```

You will need to copy a few CUDA .dll files to the location of the plugin .dll for it to run. The required .dll files from CUDA (which are located in the `bin` folder of the CUDA toolkit installation directory) are:

- `cudart64_NN.dll`
- `cublas64_NN.dll`
- `cublasLt64_NN.dll`

where `NN` is the CUDA major version number. For example, if you have installed CUDA 12.2 as in example above, then `NN` is `12`.
