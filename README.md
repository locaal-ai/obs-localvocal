# LocalVocal - Speech AI assistant OBS Plugin

<div align="center">

[![GitHub](https://img.shields.io/github/license/occ-ai/obs-localvocal)](https://github.com/occ-ai/obs-localvocal/blob/main/LICENSE)
[![GitHub Workflow Status](https://img.shields.io/github/actions/workflow/status/occ-ai/obs-localvocal/push.yaml)](https://github.com/occ-ai/obs-localvocal/actions/workflows/push.yaml)
[![Total downloads](https://img.shields.io/github/downloads/occ-ai/obs-localvocal/total)](https://github.com/occ-ai/obs-localvocal/releases)
[![GitHub release (latest by date)](https://img.shields.io/github/v/release/occ-ai/obs-localvocal)](https://github.com/occ-ai/obs-localvocal/releases)
[![Discord](https://img.shields.io/discord/1200229425141252116)](https://discord.gg/KbjGU2vvUz)

</div>

## Introduction

LocalVocal live-streaming AI assistant plugin allows you to transcribe, locally on your machine, audio speech into text and perform various language processing functions on the text using AI / LLMs (Large Language Models). ✅ No GPU required, ✅ no cloud costs, ✅ no network and ✅ no downtime! Privacy first - all data stays on your machine.

If this free plugin has been valuable to you consider adding a ⭐ to this GH repo, rating it [on OBS](https://obsproject.com/forum/resources/localvocal-live-stream-ai-assistant.1769/), subscribing to [my YouTube channel](https://www.youtube.com/@royshilk) where I post updates, and supporting my work: https://github.com/sponsors/royshil

<p align="center">
  <a href="https://youtu.be/5XqTMqpui3Q" target="_blank">
    <img width="27%" src="https://github-production-user-asset-6210df.s3.amazonaws.com/441170/267728411-334551b8-6a7f-42bf-8434-6ad6b512a401.jpeg" />
  </a>
  <a href="https://youtu.be/Q34LQsx-nlg" target="_blank">
    <img width="27%" src="https://github-production-user-asset-6210df.s3.amazonaws.com/441170/271725640-3e5edd4a-9d07-4d19-b631-c70f91c73c27.PNG" />
  </a>
  <a href="https://youtu.be/4BTmoKr0YMw" target="_blank">
    <img width="27%" src="https://github-production-user-asset-6210df.s3.amazonaws.com/441170/283315931-70c0c583-d1dc-4bd6-9ace-86c8e47f1229.jpg" />
  </a>
  <br/>
  https://youtu.be/5XqTMqpui3Q & https://youtu.be/Q34LQsx-nlg & https://youtu.be/4BTmoKr0YMw
</p>

Do more with LocalVocal:
- [Translate Caption any Application](https://youtu.be/qen7NC8kbEQ)
- [Real-time Translation with DeepL](https://youtu.be/ryWBIEmVka4)
- [POST Captions to YouTube](https://youtu.be/E7HKbO6CP_c)
- [Local LLM Real-time Translation](https://youtu.be/ZMNILPWDkDw)

Current Features:
- Transcribe audio to text in real time in 100 languages
- Display captions on screen using text sources
- Send captions to a file (which can be read by external sources)
- Send captions on a RTMP stream to e.g. YouTube, Twitch
- Bring your own Whisper model (GGML)
- Translate captions in real time to major languages
- CUDA support and Apple Arm64 support

Roadmap:
- Remove unwanted words from the transcription
- Summarize the text and show "highlights" on screen
- Detect key moments in the stream and allow triggering events (like replay)
- Detect emotions/sentiment and allow triggering events (like changing the scene or colors etc.)

Internally the plugin is running a neural network ([OpenAI Whisper](https://github.com/openai/whisper)) locally to predict in real time the speech and provide captions.

It's using the [Whisper.cpp](https://github.com/ggerganov/whisper.cpp) project from [ggerganov](https://github.com/ggerganov) to run the Whisper network in a very efficient way on CPUs and GPUs.

Check out our other plugins:
- [Background Removal](https://github.com/occ-ai/obs-backgroundremoval) removes background from webcam without a green screen.
- [Detect](https://github.com/occ-ai/obs-detect) will detect and track >80 types of objects in real-time inside OBS
- 🚧 Experimental 🚧 [CleanStream](https://github.com/occ-ai/obs-cleanstream) for real-time filler word (uh,um) and profanity removal from live audio stream
- [URL/API Source](https://github.com/occ-ai/obs-urlsource) that allows fetching live data from an API and displaying it in OBS.
- [Polyglot](https://github.com/occ-ai/obs-polyglot) translation AI plugin for real-time, local translation to hunderds of languages

## Download
Check out the [latest releases](https://github.com/occ-ai/obs-localvocal/releases) for downloads and install instructions.

### Models
The plugin ships with the Tiny.en model, and will autonomoously download other bigger Whisper models through a dropdown.
However there's an option to select an external model file if you have it on disk.

Get more models from https://ggml.ggerganov.com/ and follow [the instructions on whisper.cpp](https://github.com/ggerganov/whisper.cpp/tree/master/models) to create your own models or download others such as distilled models.

## Building

The plugin was built and tested on Mac OSX  (Intel & Apple silicon), Windows (with and without Nvidia CUDA) and Linux.

Start by cloning this repo to a directory of your choice.

### Mac OSX

Using the CI pipeline scripts, locally you would just call the zsh script, which builds for the architecture specified in $MACOS_ARCH (either `x86_64` or `arm64`).

```sh
$ MACOS_ARCH="x86_64" ./.github/scripts/build-macos -c Release
```

#### Install
The above script should succeed and the plugin files (e.g. `obs-localvocal.plugin`) will reside in the `./release/Release` folder off of the root. Copy the `.plugin` file to the OBS directory e.g. `~/Library/Application Support/obs-studio/plugins`.

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
> .github/scripts/Build-Windows.ps1 -Configuration Release
```

The build should exist in the `./release` folder off the root. You can manually install the files in the OBS directory.

```powershell
> Copy-Item -Recurse -Force "release\Release\*" -Destination "C:\Program Files\obs-studio\"
```

#### Building with CUDA support on Windows

LocalVocal will now build with CUDA support automatically through a prebuilt binary of Whisper.cpp from https://github.com/occ-ai/occ-ai-dep-whispercpp. The CMake scripts will download all necessary files.

To build with cuda add `CPU_OR_CUDA` as an environment variable (with `cpu`, `12.2.0` or `11.8.0`) and build regularly

```powershell
> $env:CPU_OR_CUDA="12.2.0"
> .github/scripts/Build-Windows.ps1 -Configuration Release
```


<picture>
  <source media="(prefers-color-scheme: dark)" srcset="https://api.star-history.com/svg?repos=occ-ai/obs-localvocal&type=Date&theme=dark" />
  <source media="(prefers-color-scheme: light)" srcset="https://api.star-history.com/svg?repos=occ-ai/obs-localvocal&type=Date" />
  <img alt="Star History Chart" src="https://api.star-history.com/svg?repos=occ-ai/obs-localvocal&type=Date" />
</picture>
