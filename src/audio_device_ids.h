#ifndef AUDIO_BRIDGE_AUDIO_DEVICE_IDS_H_
#define AUDIO_BRIDGE_AUDIO_DEVICE_IDS_H_

#include <string>
#include <string_view>

namespace audio_bridge::device_ids {

inline constexpr auto kMmePrefix = std::string_view{"mme:"};
inline constexpr auto kDirectSoundPrefix = std::string_view{"dsound:"};
inline constexpr auto kWasapiPrefix = std::string_view{"wasapi:"};
inline constexpr auto kWasapiLoopbackPrefix =
    std::string_view{"wasapi-loopback:"};
inline constexpr auto kDefaultWasapiPayload = std::string_view{"default"};

inline auto IsLegacy(const std::string& id) -> bool {
  return id.starts_with(kMmePrefix) || id.starts_with(kDirectSoundPrefix);
}

inline auto IsWasapi(const std::string& id) -> bool {
  return id.starts_with(kWasapiPrefix);
}

inline auto IsWasapiLoopback(const std::string& id) -> bool {
  return id.starts_with(kWasapiLoopbackPrefix);
}

inline auto WasapiPayload(const std::string& id) -> std::string {
  if (IsWasapiLoopback(id)) {
    return id.substr(kWasapiLoopbackPrefix.size());
  }
  return IsWasapi(id) ? id.substr(kWasapiPrefix.size()) : id;
}

}  // namespace audio_bridge::device_ids

#endif
