# Building and Using the Offline Testing tool

The offline testing tool provides a way to run the internal core transcription+translation algorithm of the OBS plugin without running OBS, in effect simulating how it would run within OBS. However, all the audio is pre-cached in memory so it runs faster than real-time (e.g. it doesn't simulate the audio input timing).
The tool is useful for automating tests to measure performance.

## Building

The tool was tested on Windows with CUDA, so this guide focuses on this setup.
However there's nothing preventing the tool to successfully build and run on Mac as well.
Linux unfortunately is not supported at the moment.

Start by cloning the repo.

Proceed to build the plugin regularly, e.g.
```powershell
obs-localvocal> $env:CPU_OR_CUDA="12.2.0"
obs-localvocal> .\.github\scripts\Build-Windows.ps1 -Configuration Release
```

Then run CMake to enable the test tool in the build
```powershell
obs-localvocal> cmake -S . -B .\build_x64\ -DENABLE_TESTS=ON
```

Once that is done you can build the test target `.exe` only (instead of building everything) e.g.
```powershell
obs-localvocal> cmake --build .\build_x64\ --target obs-localvocal-tests --config Release
obs-localvocal> copy-item -Force ".\build_x64\Release\obs-localvocal-tests.exe" -Destination ".\release\Release\test"
```

Also in the above we're copying the result `.exe` to the `./release/Release/test` folder.

Next, a few `.dll` files need to be collected and placed alongside the `obs-localvocal-test.exe` file in the `./release/Release/test` folder. Fortunately all `dll`s are available in the plugin's build folders.

For an automatic step to copy `.dll`s run the script: (run from any location, it will orient itself)
```powershell
obs-localvocal> &"src\tests\copy_dlls.ps1"
```

For manual copying follow below:

From `.\release\Release\obs-plugins\64bit` copy:

- ctranslate2.dll
- cublas64_12.dll
- cublasLt64_12.dll
- cudart64_12.dll
- libopenblas.dll
- obs-localvocal.dll
- onnxruntime_providers_shared.dll
- onnxruntime.dll
- whisper.dll

From `.deps\obs-deps-2023-11-03-x64\bin` copy:

- avcodec-60.dll
- avdevice-60.dll
- avfilter-9.dll
- avformat-60.dll
- avutil-58.dll
- libx264-164.dll
- swresample-4.dll
- swscale-7.dll
- zlib.dll

Finally, from `.deps\obs-studio-30.0.2\build_x64\rundir\Debug\bin\64bit` copy:

- obs-frontend-api.dll
- obs.dll
- w32-pthreads.dll

With all the `.dll`s in place in the `.\release\Release\test` folder the test tool should run.

## Using the test tool

The tool expects the following arguments:

- audio/video file
- whisper language
- translation source language (or `none`)
- translation target language (or `none`)
- whisper model `.bin` file
- silero VAD model file e.g. `silero_vad.onnx`
- CT2 model *folder* (whitin which the model and json files can be found)
- configuration file

The Whisper languages are listed in [whisper-language.h](../whisper-utils/whisper-language.h) and the CT2 language codes are listed in [language_codes.h](../translation/language_codes.h). They roughly match except CT2 has underscores e.g. `ko` -> `__ko__`, `ja` -> `__ja__`.

For example, this is a valid run command:

```powershell
obs-localvocal> .\release\Release\test\obs-localvocal-tests.exe "C:\Users\roysh\Downloads\audio.mp3" ko __ko__ __ja__ "C:\Users\roysh\AppData\Roaming\obs-studio\plugin_config\obs-localvocal\models\ggml-model-whisper-small\ggml-model-whisper-small.bin" "C:\Users\roysh\Downloads\obs-localvocal\data\models\silero-vad\silero_vad.onnx" "C:\Users\roysh\AppData\Roaming\obs-studio\plugin_config\obs-localvocal\models\m2m-100-418M" ".\config.json"
```

If you've used the OBS plugin to download a Whisper model and a CT2 model then you would find those in the OBS plugin config folders as visible above. It is recommended to do so.

### Configuration

The tool can receive configuration to test different parameters of the algorithm.

The JSON config file can look e.g. like
```
{
    "fix_utf8": true,
    "suppress_sentences": "끝까지 시청해주셔서 감사합니다\n구독과 좋아요 부탁드립니다!\nMBC 뉴스 안영백입니다.\nMBC 뉴스 이덕영입니다\n구독과 좋아요 눌러주세요!\n구독과 좋아요 부탁드",
    "overlap_ms": 150
}
```

Give the path to this file to the tool.

### Output

The tool would write a `output.txt` file in the running directory.

It would also output verbose running log to the console, e.g.
```
[02:07:25.148] [UNKNOWN] found 59539456 bytes, 14884864 frames in input buffer, need >= 576000
[02:07:25.150] [UNKNOWN] processing audio from buffer, 0 existing frames, 144000 frames needed to full segment (144000 frames)
[02:07:25.150] [UNKNOWN] with 144000 remaining to full segment, popped 143360 frames from info buffer, pushed at 0 (overlap)
[02:07:25.151] [UNKNOWN] first segment, no overlap exists, 143360 frames to process
[02:07:25.151] [UNKNOWN] processing 143360 frames (2986 ms), start timestamp 85
[02:07:25.154] [UNKNOWN] 2 channels, 47770 frames, 2985.625000 ms
[02:07:25.168] [UNKNOWN] VAD detected speech from 29696 to 47770 (18074 frames, 1129 ms)
[02:07:25.169] [UNKNOWN] run_whisper_inference: processing 18074 samples, 1.130 sec, 4 threads
[02:07:26.700] [UNKNOWN] Token 0: 50364, [_BEG_], p: 1.000, dtw: -1 [keep: 0]
```

### Translation

To translate with Whisper, set the whisper output language to your desired output and the CT2 languages to `none`.
For example this would be a Japanese translation with Whisper:

```powershell
obs-localvocal> .\release\Release\test\obs-localvocal-tests.exe "C:\Users\roysh\Downloads\audio.mp3" ja none none "...\ggml-model-whisper-small.bin" "...\silero_vad.onnx" "...\m2m-100-418M" ".\config.json"
```

To translate with CT2, make sure the Whisper output is in the spoken language and that it matches the source language.
For example this would be a Korean-to-Japanese translation with CT2 M2M100:

```powershell
obs-localvocal> .\release\Release\test\obs-localvocal-tests.exe "C:\Users\roysh\Downloads\audio.mp3" ko __ko__ __ja__ "...\ggml-model-whisper-small.bin" "...\silero_vad.onnx" "...\m2m-100-418M" ".\config.json"
```

## Evaluation of the results

The provided [python script](evaluate_output.py) can run WER/CER evaluation on the results.

Exmple of running the evaluation script:

```powershell
obs-localvocal> python .\src\tests\evaluate_output.py ".\ground_truth.txt" ".\output.txt"
```

It requires to install a couple packages:
```powershell
pip install Levenshtein diff_match_patch
```
