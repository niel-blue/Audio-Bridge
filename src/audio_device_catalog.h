#ifndef AUDIO_BRIDGE_DEVICE_CATALOG_H_
#define AUDIO_BRIDGE_DEVICE_CATALOG_H_

#include <string>
#include <vector>

namespace audio_bridge {

enum class DeviceBackend {
  kMme,
  kDirectSound,
  kWasapi,
  kWasapiLoopback,
};

struct AudioDeviceInfo {
  std::string id;
  std::string name;
  DeviceBackend backend = DeviceBackend::kWasapi;
  bool is_default = false;
};

struct AudioDeviceSnapshot {
  std::vector<AudioDeviceInfo> inputs;
  std::vector<AudioDeviceInfo> outputs;
  std::string error;
};

auto EnumerateAudioDevices() -> AudioDeviceSnapshot;
auto DeviceBackendLabel(DeviceBackend backend) -> const char*;

}  // namespace audio_bridge

#endif
