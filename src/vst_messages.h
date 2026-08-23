#ifndef AUDIO_BRIDGE_VST_MESSAGES_H_
#define AUDIO_BRIDGE_VST_MESSAGES_H_

#include <string>

#include "pluginterfaces/vst/ivstmessage.h"

#include "io_state.h"

namespace audio_bridge::vst::messages {

inline constexpr char kParameterSync[] = "parameter_sync";
inline constexpr char kStateSync[] = "state_sync";
inline constexpr char kAudioOff[] = "audio_off";
inline constexpr char kAudioConfig[] = "audio_config";
inline constexpr char kAudioLevels[] = "audio_levels";
inline constexpr char kHostRoutes[] = "host_routes";
inline constexpr char kAudioStatus[] = "audio_status";

inline constexpr char kInputGainDb[] = "input_gain_db";
inline constexpr char kOutputGainDb[] = "output_gain_db";
inline constexpr char kOutputDelayMs[] = "output_delay_ms";
inline constexpr char kExclusive[] = "exclusive";
inline constexpr char kInputId[] = "input_id";
inline constexpr char kOutputId[] = "output_id";
inline constexpr char kInputPeak[] = "input_peak";
inline constexpr char kOutputPeak[] = "output_peak";
inline constexpr char kInputConnected[] = "input_connected";
inline constexpr char kOutputConnected[] = "output_connected";
inline constexpr char kStatus[] = "status";
inline constexpr char kError[] = "error";

auto WriteState(Steinberg::Vst::IAttributeList* attributes,
                const state::Data& value, bool include_devices) -> bool;
auto ReadState(Steinberg::Vst::IAttributeList* attributes, state::Data& value,
               bool include_devices) -> bool;

auto WriteRouting(Steinberg::Vst::IAttributeList* attributes,
                  const std::string& input_id, const std::string& output_id,
                  bool exclusive) -> bool;
auto ReadRouting(Steinberg::Vst::IAttributeList* attributes,
                 std::string& input_id, std::string& output_id,
                 bool& exclusive) -> bool;

}  // namespace audio_bridge::vst::messages

#endif
