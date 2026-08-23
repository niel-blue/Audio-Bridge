[日本語](README.md) | [English](README_EN.md)

# Audio-Bridge

<img width="319" height="330" alt="Audio-Bridge screenshot" src="https://github.com/user-attachments/assets/0fdaf94d-5f41-4e22-a824-215510e3a812" />

Audio-Bridge is a VST plug-in that lets a DAW or VST host send audio directly to Windows audio devices. In addition to the host's standard audio input and output, it supports MME, DirectSound, and WASAPI devices.

## Features

- Input and output gain: -30 to +30 dB
- Output delay: 0 to 2000 ms
- MME, DirectSound, and WASAPI shared/exclusive modes
- Clock drift compensation between the host and external devices
- VST3 state storage for gain, delay, device selection, and other settings

When no external device is selected, Audio-Bridge behaves like a standard VST plug-in and passes audio from the host input to the host output. Selecting an external input or output switches that side to the external device route instead of mixing it with the host route.

Audio-Bridge does not open ASIO drivers directly. When used in a DAW or VST host running with ASIO, it can handle ASIO audio through the host's standard input and output route.

Audio-Bridge does not store settings in the Windows Registry or in its own configuration files. Settings are restored through the DAW or VST host's project and preset storage.

## Requirements

- Windows x64
- A DAW or VST host with VST3 support

## Installation

Extract the ZIP from the Releases page and copy `Audio-Bridge.vst3` to your VST3 plug-in folder.

## Build

Visual Studio 2022 with the C++ tools and CMake 3.25 or later are required.

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

The pinned VST3 SDK version is downloaded automatically during the first configuration.

## License

Audio-Bridge is released under the [MIT License](LICENSE). See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for third-party notices.
