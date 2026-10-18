[日本語](README.md) | [English](README_EN.md)

# Audio-Bridge

Current release: `0.4.4`

<img width="319" height="330" alt="Audio-Bridge screenshot" src="https://github.com/user-attachments/assets/0fdaf94d-5f41-4e22-a824-215510e3a812" />

Audio-Bridge is a VST plug-in that lets a DAW or VST host route audio directly to and from Windows audio devices. In addition to the host's standard audio input and output, it supports MME, DirectSound, WASAPI, and per-application audio input.

## Features

- Input and output gain: -20 to +20 dB (independent L/R, linkable, individual mutes)
- Input and output delay: 0 to 10,000 ms (knob range to 1,000 ms; manual entry to 10,000 ms)
- Per-application audio input through Application Input
- MME, DirectSound, and WASAPI shared/exclusive modes
- Clock drift compensation between the host and external devices
- VST3 state storage for gain, delay, device selection, and other settings

INPUT selects HOST INPUT or an external input. HOST INPUT is the default and applies input gain and input delay to the host signal. When an external input is selected, it replaces the host input and input gain and input delay apply only to that selected source. ADDITIONAL OUTPUT sends the selected input to the host output and an external destination. Output gain and delay affect host output when HOST OUTPUT is selected, and only the external output when an external destination is selected. While external output is selected, DAW output carries the input after input gain and input delay; output gain and output delay affect only the external output.

States from 0.3.1 remain readable. External output branches instead of muting the host output. Users must manage duplicate audio, excessive levels, feedback loops and timing differences through routing and gain. No automatic normalization or delay alignment is applied.

Choose an application with a Windows render-audio session from the `[Application Input]` entries under INPUT to capture only that application's process tree. This is per-application input, not the old system-wide loopback source. Use `REFRESH` to update the application list. An application can appear only after it has started producing audio. The VST state stores only its executable identity; the current process ID is resolved each time the source starts.

Audio-Bridge does not open ASIO drivers directly. When used in a DAW or VST host running with ASIO, it can handle ASIO audio through the host's standard input and output route.

Audio-Bridge does not store settings in the Windows Registry or in its own configuration files. Settings are restored through the DAW or VST host's project and preset storage.

## Requirements

- Windows 10 version 2004 or later, or Windows 11 x64 when using Application Input
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
