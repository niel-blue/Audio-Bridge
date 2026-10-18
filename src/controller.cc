#include "controller.h"

#include "editor.h"
#include "io_state.h"
#include "parameters.h"
#include "vst_messages.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <utility>

#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/base/smartpointer.h"
#include "public.sdk/source/vst/vstparameters.h"

namespace audio_bridge::vst {
namespace {

using Steinberg::kResultFalse;
using Steinberg::kResultTrue;

}  // namespace

Controller::~Controller() = default;

auto PLUGIN_API Controller::initialize(Steinberg::FUnknown* const context)
    -> tresult {
  const auto result = EditController::initialize(context);
  if (result != kResultTrue) {
    return result;
  }
  parameters.addParameter(new Steinberg::Vst::RangeParameter(
      STR16("Input Gain L"), kInputGain, STR16("dB"), kMinimumGainDb,
      kMaximumGainDb, 0.0, 0, Steinberg::Vst::ParameterInfo::kCanAutomate));
  parameters.addParameter(new Steinberg::Vst::RangeParameter(
      STR16("Output Gain L"), kOutputGain, STR16("dB"), kMinimumGainDb,
      kMaximumGainDb, 0.0, 0, Steinberg::Vst::ParameterInfo::kCanAutomate));
  parameters.addParameter(new Steinberg::Vst::Parameter(
      STR16("WASAPI Exclusive"), kExclusive, nullptr, 0.0, 1,
      Steinberg::Vst::ParameterInfo::kCanAutomate));
  parameters.addParameter(new Steinberg::Vst::RangeParameter(
      STR16("Output Delay"), kOutputDelay, STR16("ms"), 0.0,
      kMaximumDelayMs, 0.0, 0,
      Steinberg::Vst::ParameterInfo::kCanAutomate));
  parameters.addParameter(new Steinberg::Vst::RangeParameter(
      STR16("Input Delay"), kInputDelay, STR16("ms"), 0.0,
      kMaximumDelayMs, 0.0, 0,
      Steinberg::Vst::ParameterInfo::kCanAutomate));
  parameters.addParameter(new Steinberg::Vst::RangeParameter(STR16("Input Gain R"), kInputGainRight, STR16("dB"), kMinimumGainDb, kMaximumGainDb, 0.0, 0, Steinberg::Vst::ParameterInfo::kCanAutomate));
  parameters.addParameter(new Steinberg::Vst::RangeParameter(STR16("Output Gain R"), kOutputGainRight, STR16("dB"), kMinimumGainDb, kMaximumGainDb, 0.0, 0, Steinberg::Vst::ParameterInfo::kCanAutomate));
  parameters.addParameter(new Steinberg::Vst::Parameter(STR16("Input Link"), kInputLink, nullptr, 1.0, 1, Steinberg::Vst::ParameterInfo::kCanAutomate));
  parameters.addParameter(new Steinberg::Vst::Parameter(STR16("Output Link"), kOutputLink, nullptr, 1.0, 1, Steinberg::Vst::ParameterInfo::kCanAutomate));
  parameters.addParameter(new Steinberg::Vst::Parameter(STR16("Input Mute L"), kInputMuteLeft, nullptr, 0.0, 1, Steinberg::Vst::ParameterInfo::kCanAutomate));
  parameters.addParameter(new Steinberg::Vst::Parameter(STR16("Input Mute R"), kInputMuteRight, nullptr, 0.0, 1, Steinberg::Vst::ParameterInfo::kCanAutomate));
  parameters.addParameter(new Steinberg::Vst::Parameter(STR16("Output Mute L"), kOutputMuteLeft, nullptr, 0.0, 1, Steinberg::Vst::ParameterInfo::kCanAutomate));
  parameters.addParameter(new Steinberg::Vst::Parameter(STR16("Output Mute R"), kOutputMuteRight, nullptr, 0.0, 1, Steinberg::Vst::ParameterInfo::kCanAutomate));
  return kResultTrue;
}

auto PLUGIN_API Controller::setComponentState(IBStream* const state)
    -> tresult {
  return setState(state);
}

auto PLUGIN_API Controller::setState(IBStream* const state) -> tresult {
  auto data = state::Data{};
  if (!state::Read(state, data)) {
    return kResultFalse;
  }
  ApplyState(data);
  SendStateToProcessor(data, true);
  return kResultTrue;
}

auto PLUGIN_API Controller::getState(IBStream* const state) -> tresult {
  return state::Write(state, CurrentState()) ? kResultTrue : kResultFalse;
}

auto PLUGIN_API Controller::setParamNormalized(ParamID id, ParamValue value) -> tresult {
  const auto result = EditController::setParamNormalized(id, value);
  if (result != Steinberg::kResultTrue) return result;
  for (const auto input : {true, false}) {
    const auto left = input ? kInputGain : kOutputGain;
    const auto right = input ? kInputGainRight : kOutputGainRight;
    const auto link = input ? kInputLink : kOutputLink;
    if (getParamNormalized(link) > 0.5) {
      if (id == left || id == right)
        EditController::setParamNormalized(id == left ? right : left, value);
      else if (id == link)
        EditController::setParamNormalized(right, getParamNormalized(left));
    }
  }
  return result;
}

void Controller::SyncParameterStateToProcessor() {
  SendStateToProcessor(CurrentState(), false);
}

auto Controller::CurrentState() -> state::Data {
  auto data = state::Data{};
  data.input_gain_db = GainDbFromNormalized(getParamNormalized(kInputGain));
  data.output_gain_db = GainDbFromNormalized(getParamNormalized(kOutputGain));
  data.output_delay_ms =
      DelayMsFromNormalized(getParamNormalized(kOutputDelay));
  data.input_delay_ms =
      DelayMsFromNormalized(getParamNormalized(kInputDelay));
  data.input_gain_right_db = GainDbFromNormalized(getParamNormalized(kInputGainRight));
  data.output_gain_right_db = GainDbFromNormalized(getParamNormalized(kOutputGainRight));
  data.input_link = getParamNormalized(kInputLink) > 0.5;
  data.output_link = getParamNormalized(kOutputLink) > 0.5;
  data.input_mute_left = getParamNormalized(kInputMuteLeft) > 0.5;
  data.input_mute_right = getParamNormalized(kInputMuteRight) > 0.5;
  data.output_mute_left = getParamNormalized(kOutputMuteLeft) > 0.5;
  data.output_mute_right = getParamNormalized(kOutputMuteRight) > 0.5;
  data.exclusive = getParamNormalized(kExclusive) > 0.5;
  auto ignored_exclusive = false;
  GetDeviceSelection(data.input_id, data.output_id, ignored_exclusive);
  return data;
}

void Controller::ApplyState(const state::Data& data) {
  static_cast<void>(
      EditController::setParamNormalized(kInputGain, GainDbToNormalized(data.input_gain_db)));
  static_cast<void>(EditController::setParamNormalized(
      kOutputGain, GainDbToNormalized(data.output_gain_db)));
  static_cast<void>(EditController::setParamNormalized(
      kOutputDelay, DelayMsToNormalized(data.output_delay_ms)));
  static_cast<void>(EditController::setParamNormalized(
      kInputDelay, DelayMsToNormalized(data.input_delay_ms)));
  static_cast<void>(
      EditController::setParamNormalized(kExclusive, data.exclusive ? 1.0 : 0.0));
  static_cast<void>(EditController::setParamNormalized(kInputGainRight, GainDbToNormalized(data.input_gain_right_db)));
  static_cast<void>(EditController::setParamNormalized(kOutputGainRight, GainDbToNormalized(data.output_gain_right_db)));
  static_cast<void>(EditController::setParamNormalized(kInputLink, data.input_link ? 1.0 : 0.0));
  static_cast<void>(EditController::setParamNormalized(kOutputLink, data.output_link ? 1.0 : 0.0));
  static_cast<void>(EditController::setParamNormalized(kInputMuteLeft, data.input_mute_left ? 1.0 : 0.0));
  static_cast<void>(EditController::setParamNormalized(kInputMuteRight, data.input_mute_right ? 1.0 : 0.0));
  static_cast<void>(EditController::setParamNormalized(kOutputMuteLeft, data.output_mute_left ? 1.0 : 0.0));
  static_cast<void>(EditController::setParamNormalized(kOutputMuteRight, data.output_mute_right ? 1.0 : 0.0));
  SetDeviceSelection(data.input_id, data.output_id, data.exclusive);
}

void Controller::SendStateToProcessor(const state::Data& data,
                                      const bool include_devices) {
  if (const auto message = Steinberg::owned(allocateMessage())) {
    message->setMessageID(include_devices ? messages::kStateSync
                                          : messages::kParameterSync);
    if (messages::WriteState(message->getAttributes(), data,
                             include_devices)) {
      static_cast<void>(sendMessage(message));
    }
  }
}

auto PLUGIN_API Controller::createView(const char* const name)
    -> Steinberg::IPlugView* {
  if (name != nullptr && std::strcmp(name, "editor") == 0) {
    return new Editor(this);
  }
  return nullptr;
}

auto PLUGIN_API Controller::notify(IMessage* const message) -> tresult {
  if (message == nullptr || message->getAttributes() == nullptr) {
    return kResultFalse;
  }
  auto* const attributes = message->getAttributes();
  if (std::strcmp(message->getMessageID(), messages::kAudioLevels) == 0) {
    double input = 0.0;
    double output = 0.0;
    if (attributes->getFloat(messages::kInputPeak, input) != kResultTrue ||
        attributes->getFloat(messages::kOutputPeak, output) != kResultTrue) {
      return kResultFalse;
    }
    input_peak_.store(static_cast<float>(std::clamp(input, 0.0, 2.0)),
                      std::memory_order_relaxed);
    output_peak_.store(static_cast<float>(std::clamp(output, 0.0, 2.0)),
                       std::memory_order_relaxed);
    return kResultTrue;
  }
  if (std::strcmp(message->getMessageID(), messages::kHostRoutes) == 0) {
    Steinberg::int64 input_connected = 0;
    Steinberg::int64 output_connected = 0;
    if (attributes->getInt(messages::kInputConnected, input_connected) !=
            kResultTrue ||
        attributes->getInt(messages::kOutputConnected, output_connected) !=
            kResultTrue) {
      return kResultFalse;
    }
    host_input_connected_.store(input_connected != 0,
                                std::memory_order_release);
    host_output_connected_.store(output_connected != 0,
                                 std::memory_order_release);
    return kResultTrue;
  }
  if (std::strcmp(message->getMessageID(), messages::kAudioStatus) == 0) {
    Steinberg::int64 value = 0;
    if (attributes->getInt(messages::kStatus, value) != kResultTrue) {
      return kResultFalse;
    }
    value = std::clamp<Steinberg::int64>(value, 0, 3);
    auto error = std::string{};
    Steinberg::uint32 size = 0U;
    const void* data = nullptr;
    if (attributes->getBinary(messages::kError, data, size) == kResultTrue &&
        data != nullptr && size > 0U) {
      error.assign(static_cast<const char*>(data), size);
    }
    status_.store(static_cast<audio::Status>(value),
                  std::memory_order_release);
    const auto lock = std::lock_guard(device_mutex_);
    status_error_ = std::move(error);
    return kResultTrue;
  }
  return EditController::notify(message);
}

void Controller::SetDeviceSelection(std::string input_id,
                                     std::string output_id,
                                     const bool exclusive) {
  const auto lock = std::lock_guard(device_mutex_);
  input_id_ = std::move(input_id);
  output_id_ = std::move(output_id);
  exclusive_ = exclusive;
}

void Controller::GetDeviceSelection(std::string& input_id,
                                     std::string& output_id,
                                     bool& exclusive) const {
  const auto lock = std::lock_guard(device_mutex_);
  input_id = input_id_;
  output_id = output_id_;
  exclusive = exclusive_;
}

void Controller::GetHostRoutes(bool& input_connected,
                               bool& output_connected) const {
  input_connected = host_input_connected_.load(std::memory_order_acquire);
  output_connected = host_output_connected_.load(std::memory_order_acquire);
}

void Controller::GetAudioLevels(float& input_peak, float& output_peak) const {
  input_peak = input_peak_.load(std::memory_order_relaxed);
  output_peak = output_peak_.load(std::memory_order_relaxed);
}

void Controller::GetAudioStatus(audio::Status& status,
                                 std::string& error) const {
  status = status_.load(std::memory_order_acquire);
  const auto lock = std::lock_guard(device_mutex_);
  error = status_error_;
}

}  // namespace audio_bridge::vst
