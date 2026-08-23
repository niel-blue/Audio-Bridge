#include "audio_device_catalog.h"

#ifdef _WIN32

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <mmsystem.h>
#include <mmdeviceapi.h>
#include <propkeydef.h>
#include <functiondiscoverykeys_devpkey.h>
#include <propvarutil.h>
#include <dsound.h>
#include <wrl/client.h>

#include <string>
#include <iterator>
#include <utility>

namespace audio_bridge {
namespace {

using Microsoft::WRL::ComPtr;

auto WideToUtf8(const wchar_t* const text) -> std::string {
  if (text == nullptr || text[0] == L'\0') {
    return {};
  }
  const auto size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0,
                                        nullptr, nullptr);
  if (size <= 1) {
    return {};
  }
  auto result = std::string(static_cast<std::size_t>(size), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text, -1, result.data(), size, nullptr,
                      nullptr);
  result.resize(static_cast<std::size_t>(size - 1));
  return result;
}

auto WasapiRawId(IMMDevice* const device) -> std::string {
  LPWSTR raw_id = nullptr;
  if (device == nullptr || FAILED(device->GetId(&raw_id))) {
    return {};
  }
  const auto result = WideToUtf8(raw_id);
  CoTaskMemFree(raw_id);
  return result;
}

auto WasapiName(IMMDevice* const device) -> std::string {
  ComPtr<IPropertyStore> properties;
  if (device == nullptr ||
      FAILED(device->OpenPropertyStore(STGM_READ, &properties))) {
    return "Unnamed audio device";
  }
  PROPVARIANT value;
  PropVariantInit(&value);
  auto result = std::string("Unnamed audio device");
  if (SUCCEEDED(properties->GetValue(PKEY_Device_FriendlyName, &value)) &&
      value.vt == VT_LPWSTR) {
    const auto name = WideToUtf8(value.pwszVal);
    if (!name.empty()) {
      result = name;
    }
  }
  PropVariantClear(&value);
  return result;
}

auto EnumerateWasapiDirection(IMMDeviceEnumerator* const enumerator,
                              const EDataFlow direction)
    -> std::vector<AudioDeviceInfo> {
  auto default_id = std::string{};
  ComPtr<IMMDevice> default_device;
  if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(direction, eConsole,
                                                     &default_device))) {
    default_id = WasapiRawId(default_device.Get());
  }

  ComPtr<IMMDeviceCollection> collection;
  if (FAILED(enumerator->EnumAudioEndpoints(direction, DEVICE_STATE_ACTIVE,
                                            &collection))) {
    return {};
  }
  UINT count = 0;
  if (FAILED(collection->GetCount(&count))) {
    return {};
  }

  auto result = std::vector<AudioDeviceInfo>{};
  result.reserve(count);
  for (UINT index = 0; index < count; ++index) {
    ComPtr<IMMDevice> device;
    if (FAILED(collection->Item(index, &device))) {
      continue;
    }
    const auto raw_id = WasapiRawId(device.Get());
    if (raw_id.empty()) {
      continue;
    }
    result.push_back({"wasapi:" + raw_id, WasapiName(device.Get()),
                     DeviceBackend::kWasapi, raw_id == default_id});
  }
  return result;
}

auto MmeName(const wchar_t* const name, const char* const fallback)
    -> std::string {
  const auto result = WideToUtf8(name);
  return result.empty() ? std::string(fallback) : result;
}

auto EnumerateMmeInputs() -> std::vector<AudioDeviceInfo> {
  const auto count = waveInGetNumDevs();
  auto result = std::vector<AudioDeviceInfo>{};
  result.reserve(count);
  for (UINT index = 0; index < count; ++index) {
    WAVEINCAPSW caps{};
    if (waveInGetDevCapsW(static_cast<UINT_PTR>(index), &caps,
                          sizeof(caps)) != MMSYSERR_NOERROR) {
      continue;
    }
    result.push_back({"mme:input:" + std::to_string(index),
                     MmeName(caps.szPname, "MME input device"),
                     DeviceBackend::kMme, false});
  }
  return result;
}

auto EnumerateMmeOutputs() -> std::vector<AudioDeviceInfo> {
  const auto count = waveOutGetNumDevs();
  auto result = std::vector<AudioDeviceInfo>{};
  result.reserve(count);
  for (UINT index = 0; index < count; ++index) {
    WAVEOUTCAPSW caps{};
    if (waveOutGetDevCapsW(static_cast<UINT_PTR>(index), &caps,
                           sizeof(caps)) != MMSYSERR_NOERROR) {
      continue;
    }
    result.push_back({"mme:output:" + std::to_string(index),
                     MmeName(caps.szPname, "MME output device"),
                     DeviceBackend::kMme, false});
  }
  return result;
}

auto GuidText(LPGUID const guid) -> std::string {
  if (guid == nullptr) {
    return "default";
  }
  wchar_t text[64]{};
  if (StringFromGUID2(*guid, text, static_cast<int>(std::size(text))) <= 0) {
    return "unknown";
  }
  return WideToUtf8(text);
}

struct DirectSoundEnumerationContext {
  std::vector<AudioDeviceInfo>* devices = nullptr;
  bool input = false;
};

BOOL CALLBACK DirectSoundEnumerationCallback(LPGUID guid, LPCWSTR description,
                                             LPCWSTR /*module*/,
                                             LPVOID context) {
  auto* const data = static_cast<DirectSoundEnumerationContext*>(context);
  if (data == nullptr || data->devices == nullptr) {
    return TRUE;
  }
  const auto direction = data->input ? "input" : "output";
  const auto prefix = std::string("dsound:") + direction + ":";
  auto name = WideToUtf8(description);
  if (name.empty()) {
    name = data->input ? "DirectSound input device"
                       : "DirectSound output device";
  }
  data->devices->push_back({prefix + GuidText(guid), std::move(name),
                            DeviceBackend::kDirectSound, guid == nullptr});
  return TRUE;
}

auto EnumerateDirectSoundInputs() -> std::vector<AudioDeviceInfo> {
  auto result = std::vector<AudioDeviceInfo>{};
  auto context = DirectSoundEnumerationContext{&result, true};
  static_cast<void>(DirectSoundCaptureEnumerateW(
      DirectSoundEnumerationCallback, &context));
  return result;
}

auto EnumerateDirectSoundOutputs() -> std::vector<AudioDeviceInfo> {
  auto result = std::vector<AudioDeviceInfo>{};
  auto context = DirectSoundEnumerationContext{&result, false};
  static_cast<void>(DirectSoundEnumerateW(DirectSoundEnumerationCallback,
                                          &context));
  return result;
}

}  // namespace

auto DeviceBackendLabel(const DeviceBackend backend) -> const char* {
  switch (backend) {
    case DeviceBackend::kMme:
      return "MME";
    case DeviceBackend::kDirectSound:
      return "Direct Sound";
    case DeviceBackend::kWasapi:
      return "WASAPI";
  }
  return "Audio";
}

auto EnumerateAudioDevices() -> AudioDeviceSnapshot {
  auto snapshot = AudioDeviceSnapshot{};
  snapshot.inputs = EnumerateMmeInputs();
  snapshot.outputs = EnumerateMmeOutputs();

  auto direct_inputs = EnumerateDirectSoundInputs();
  auto direct_outputs = EnumerateDirectSoundOutputs();
  snapshot.inputs.insert(snapshot.inputs.end(), direct_inputs.begin(),
                         direct_inputs.end());
  snapshot.outputs.insert(snapshot.outputs.end(), direct_outputs.begin(),
                          direct_outputs.end());

  const auto com_result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  const auto uninitialize = SUCCEEDED(com_result);
  if (FAILED(com_result) && com_result != RPC_E_CHANGED_MODE) {
    snapshot.error = "Windows audio initialization failed.";
    return snapshot;
  }

  ComPtr<IMMDeviceEnumerator> enumerator;
  const auto create_result = CoCreateInstance(
      __uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
      IID_PPV_ARGS(&enumerator));
  if (SUCCEEDED(create_result)) {
    auto wasapi_inputs = EnumerateWasapiDirection(enumerator.Get(), eCapture);
    auto wasapi_outputs = EnumerateWasapiDirection(enumerator.Get(), eRender);
    snapshot.inputs.insert(snapshot.inputs.end(), wasapi_inputs.begin(),
                           wasapi_inputs.end());
    snapshot.outputs.insert(snapshot.outputs.end(), wasapi_outputs.begin(),
                            wasapi_outputs.end());
  } else {
    snapshot.error = "WASAPI devices could not be enumerated.";
  }
  if (uninitialize) {
    CoUninitialize();
  }
  return snapshot;
}

}  // namespace audio_bridge

#else

namespace audio_bridge {

auto DeviceBackendLabel(const DeviceBackend backend) -> const char* {
  switch (backend) {
    case DeviceBackend::kMme:
      return "MME";
    case DeviceBackend::kDirectSound:
      return "Direct Sound";
    case DeviceBackend::kWasapi:
      return "WASAPI";
  }
  return "Audio";
}

auto EnumerateAudioDevices() -> AudioDeviceSnapshot {
  return {.error = "Windows audio devices are only available on Windows."};
}

}  // namespace audio_bridge

#endif
