#ifndef AUDIO_BRIDGE_IO_STATE_H_
#define AUDIO_BRIDGE_IO_STATE_H_

#include <string>

#include "pluginterfaces/base/ibstream.h"

namespace audio_bridge::state {

struct Data {
  double input_gain_db = 0.0;
  double output_gain_db = 0.0;
  double output_delay_ms = 0.0;
  double input_delay_ms = 0.0;
  double input_gain_right_db = 0.0;
  double output_gain_right_db = 0.0;
  bool input_link = true;
  bool output_link = true;
  bool input_mute_left = false;
  bool input_mute_right = false;
  bool output_mute_left = false;
  bool output_mute_right = false;
  bool exclusive = false;
  std::string input_id;
  std::string output_id;
};

auto Sanitize(Data value) -> Data;
auto Read(Steinberg::IBStream* stream, Data& value) -> bool;
auto Write(Steinberg::IBStream* stream, const Data& value) -> bool;

}  // namespace audio_bridge::state

#endif
