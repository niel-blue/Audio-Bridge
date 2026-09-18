#ifndef AUDIO_BRIDGE_AUDIO_DEVICE_IDS_H_
#define AUDIO_BRIDGE_AUDIO_DEVICE_IDS_H_

#include <string>
#include <string_view>

namespace audio_bridge::device_ids {

inline constexpr auto kMmePrefix = std::string_view{"mme:"};
inline constexpr auto kDirectSoundPrefix = std::string_view{"dsound:"};
inline constexpr auto kWasapiPrefix = std::string_view{"wasapi:"};
inline constexpr auto kDefaultWasapiPayload = std::string_view{"default"};
inline constexpr auto kApplicationInputPrefix =
    std::string_view{"application-input:"};

inline auto IsLegacy(const std::string& id) -> bool {
  return id.starts_with(kMmePrefix) || id.starts_with(kDirectSoundPrefix);
}

inline auto IsWasapi(const std::string& id) -> bool {
  return id.starts_with(kWasapiPrefix);
}

inline auto IsApplicationInput(const std::string& id) -> bool {
  return id.starts_with(kApplicationInputPrefix);
}

inline auto WasapiPayload(const std::string& id) -> std::string {
  return IsWasapi(id) ? id.substr(kWasapiPrefix.size()) : id;
}

inline auto ApplicationInputPayload(const std::string& id) -> std::string {
  return IsApplicationInput(id)
             ? id.substr(kApplicationInputPrefix.size())
             : std::string{};
}

inline auto MakeApplicationInputId(const std::string_view identity)
    -> std::string {
  return std::string(kApplicationInputPrefix) + std::string(identity);
}

}  // namespace audio_bridge::device_ids

#endif
