#include "processor.h"

#include "io_state.h"
#include "parameters.h"
#include "vst_messages.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

#include "pluginterfaces/base/smartpointer.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/vstspeaker.h"

#include "metadata.h"

namespace audio_bridge::vst {
namespace {

using Steinberg::kResultFalse;
using Steinberg::kResultOk;
using Steinberg::kResultTrue;
namespace SpeakerArr = Steinberg::Vst::SpeakerArr;

auto AmpFromDb(const double db) -> float {
  return static_cast<float>(std::pow(10.0, db / 20.0));
}

}  // namespace

Processor::Processor()
    : audio_io_([this](const audio::Status status, const std::string& error) {
        SendAudioStatus(status, error);
      }) {
  setControllerClass(kControllerUID);
}

Processor::~Processor() { audio_io_.Stop(); }

auto PLUGIN_API Processor::initialize(Steinberg::FUnknown* const context)
    -> tresult {
  if (AudioEffect::initialize(context) != kResultTrue) {
    return kResultFalse;
  }
  addAudioInput(STR16("Audio Input"), SpeakerArr::kStereo);
  addAudioOutput(STR16("Audio Output"), SpeakerArr::kStereo);
  return kResultTrue;
}

auto PLUGIN_API Processor::setBusArrangements(
    SpeakerArrangement* const inputs, const int32 num_ins,
    SpeakerArrangement* const outputs, const int32 num_outs) -> tresult {
  if (inputs != nullptr && outputs != nullptr && num_ins == 1 && num_outs == 1 &&
      (inputs[0] == SpeakerArr::kMono || inputs[0] == SpeakerArr::kStereo) &&
      (outputs[0] == SpeakerArr::kMono ||
       outputs[0] == SpeakerArr::kStereo)) {
    return AudioEffect::setBusArrangements(inputs, num_ins, outputs, num_outs);
  }
  return kResultFalse;
}

auto PLUGIN_API Processor::setupProcessing(ProcessSetup& setup) -> tresult {
  if (setup.symbolicSampleSize == Steinberg::Vst::kSample64 ||
      setup.sampleRate <= 0.0 || setup.maxSamplesPerBlock <= 0) {
    return kResultFalse;
  }
  sample_rate_ = setup.sampleRate;
  meter_frames_ = 0;
  input_peak_ = 0.0F;
  output_peak_ = 0.0F;
  output_delay_.Prepare(sample_rate_, kMaximumOutputDelayMs);
  output_delay_.Reset();
  const auto size = static_cast<std::size_t>(setup.maxSamplesPerBlock);
  direct_input_left_.assign(size, 0.0F);
  direct_input_right_.assign(size, 0.0F);
  output_left_.assign(size, 0.0F);
  output_right_.assign(size, 0.0F);
  StartConfiguredIo();
  return AudioEffect::setupProcessing(setup);
}

auto PLUGIN_API Processor::getLatencySamples() -> uint32 {
  if (sample_rate_ <= 0.0) {
    return 0U;
  }
  return static_cast<uint32>(std::llround(
      output_delay_ms_.load(std::memory_order_relaxed) * sample_rate_ /
      1000.0));
}

auto PLUGIN_API Processor::setActive(const TBool state) -> tresult {
  if (state) {
    StartConfiguredIo();
  } else {
    audio_io_.Stop();
    meter_frames_ = 0;
    input_peak_ = 0.0F;
    output_peak_ = 0.0F;
    output_delay_.Reset();
  }
  return AudioEffect::setActive(state);
}

void Processor::StartConfiguredIo() {
  std::optional<audio::Config> config;
  {
    const auto lock = std::lock_guard(state_mutex_);
    config = config_;
  }
  if (!config.has_value() || sample_rate_ <= 0.0) {
    return;
  }
  config->host_sample_rate = sample_rate_;
  config->exclusive = exclusive_.load(std::memory_order_relaxed);
  audio_io_.Start(*config);
}

auto PLUGIN_API Processor::process(ProcessData& data) -> tresult {
  if (data.numSamples <= 0 || data.numOutputs <= 0 || data.outputs == nullptr ||
      data.outputs[0].numChannels <= 0 ||
      data.symbolicSampleSize == Steinberg::Vst::kSample64) {
    return kResultOk;
  }
  const auto samples = static_cast<std::size_t>(data.numSamples);
  if (samples > direct_input_left_.size()) {
    return kResultFalse;
  }
  if (reset_output_delay_.exchange(false, std::memory_order_acq_rel)) {
    output_delay_.Reset();
  }

  if (data.inputParameterChanges != nullptr) {
    const auto count = data.inputParameterChanges->getParameterCount();
    for (auto index = 0; index < count; ++index) {
      auto* const queue = data.inputParameterChanges->getParameterData(index);
      if (queue == nullptr || queue->getPointCount() <= 0) {
        continue;
      }
      int32 sample_offset = 0;
      ParamValue value = 0.0;
      if (queue->getPoint(queue->getPointCount() - 1, sample_offset, value) !=
          kResultTrue) {
        continue;
      }
      if (queue->getParameterId() == kInputGain) {
        input_gain_db_.store(GainDbFromNormalized(value),
                             std::memory_order_relaxed);
      } else if (queue->getParameterId() == kOutputGain) {
        output_gain_db_.store(GainDbFromNormalized(value),
                              std::memory_order_relaxed);
      } else if (queue->getParameterId() == kOutputDelay) {
        output_delay_ms_.store(DelayMsFromNormalized(value),
                               std::memory_order_relaxed);
      } else if (queue->getParameterId() == kExclusive) {
        exclusive_.store(value > 0.5, std::memory_order_relaxed);
      }
    }
  }

  const auto input_gain = AmpFromDb(input_gain_db_.load());
  const auto output_gain = AmpFromDb(output_gain_db_.load());
  const auto* const host_left =
      data.inputs != nullptr && data.numInputs > 0 &&
              data.inputs[0].numChannels > 0 &&
              data.inputs[0].channelBuffers32 != nullptr
          ? data.inputs[0].channelBuffers32[0]
          : nullptr;
  const auto* const host_right =
      data.inputs != nullptr && data.numInputs > 0 &&
              data.inputs[0].numChannels > 1 &&
              data.inputs[0].channelBuffers32 != nullptr
          ? data.inputs[0].channelBuffers32[1]
          : host_left;
  auto* const out_left = data.outputs[0].channelBuffers32 != nullptr
                             ? data.outputs[0].channelBuffers32[0]
                             : nullptr;
  auto* const out_right = data.outputs[0].numChannels > 1 &&
                                  data.outputs[0].channelBuffers32 != nullptr
                              ? data.outputs[0].channelBuffers32[1]
                              : nullptr;
  const auto host_input_connected = host_left != nullptr;
  const auto host_output_connected = out_left != nullptr;
  if (host_input_connected != host_input_connected_ ||
      host_output_connected != host_output_connected_) {
    host_input_connected_ = host_input_connected;
    host_output_connected_ = host_output_connected;
    SendHostRoutes(host_input_connected, host_output_connected);
  }
  if (out_left == nullptr) {
    return kResultOk;
  }

  const auto use_external_input = audio_io_.InputEnabled();
  const auto use_external_output = audio_io_.OutputEnabled();
  if (use_external_input) {
    audio_io_.PopInputBlock(direct_input_left_.data(),
                            direct_input_right_.data(), samples);
  }

  auto block_input_peak = 0.0F;
  auto block_output_peak = 0.0F;
  for (std::size_t index = 0; index < samples; ++index) {
    const auto source_left = use_external_input
                                 ? direct_input_left_[index]
                                 : (host_left != nullptr ? host_left[index]
                                                          : 0.0F);
    const auto source_right = use_external_input
                                   ? direct_input_right_[index]
                                   : (host_right != nullptr ? host_right[index]
                                                            : source_left);
    const auto processed_left = source_left * input_gain;
    const auto processed_right = source_right * input_gain;
    const auto left = processed_left * output_gain;
    const auto right = processed_right * output_gain;
    block_input_peak = std::max(block_input_peak,
                                std::max(std::abs(processed_left),
                                         std::abs(processed_right)));
    output_left_[index] = left;
    output_right_[index] = right;
  }

  output_delay_.Process(output_left_.data(), output_right_.data(), samples,
                        output_delay_ms_.load(std::memory_order_relaxed));
  for (std::size_t index = 0; index < samples; ++index) {
    block_output_peak = std::max(
        block_output_peak,
        std::max(std::abs(output_left_[index]), std::abs(output_right_[index])));
  }

  auto silence_flags = Steinberg::uint64{0U};
  if (use_external_output) {
    std::fill_n(out_left, samples, 0.0F);
    silence_flags |= 1U;
    if (out_right != nullptr) {
      std::fill_n(out_right, samples, 0.0F);
      silence_flags |= 2U;
    }
  } else {
    auto left_silent = true;
    auto right_silent = true;
    for (std::size_t index = 0; index < samples; ++index) {
      out_left[index] = output_left_[index];
      left_silent = left_silent && output_left_[index] == 0.0F;
      if (out_right != nullptr) {
        out_right[index] = output_right_[index];
        right_silent = right_silent && output_right_[index] == 0.0F;
      }
    }
    if (left_silent) {
      silence_flags |= 1U;
    }
    if (out_right != nullptr && right_silent) {
      silence_flags |= 2U;
    }
  }
  data.outputs[0].silenceFlags = silence_flags;
  input_peak_ = std::max(input_peak_, block_input_peak);
  output_peak_ = std::max(output_peak_, block_output_peak);
  if (use_external_output) {
    audio_io_.PushOutputBlock(output_left_.data(), output_right_.data(), samples);
  }

  meter_frames_ += data.numSamples;
  const auto interval = std::max<std::int64_t>(
      1, static_cast<std::int64_t>(std::llround(sample_rate_ * 0.05)));
  if (meter_frames_ >= interval) {
    SendAudioLevels();
    meter_frames_ = 0;
    input_peak_ = 0.0F;
    output_peak_ = 0.0F;
  }
  return kResultOk;
}

void Processor::SendAudioLevels() {
  if (const auto message = Steinberg::owned(allocateMessage())) {
    message->setMessageID(messages::kAudioLevels);
    if (auto* const attributes = message->getAttributes(); attributes != nullptr) {
      static_cast<void>(
          attributes->setFloat(messages::kInputPeak, input_peak_));
      static_cast<void>(
          attributes->setFloat(messages::kOutputPeak, output_peak_));
      static_cast<void>(sendMessage(message));
    }
  }
}

void Processor::SendHostRoutes(const bool input_connected,
                               const bool output_connected) {
  if (const auto message = Steinberg::owned(allocateMessage())) {
    message->setMessageID(messages::kHostRoutes);
    if (auto* const attributes = message->getAttributes(); attributes != nullptr) {
      static_cast<void>(attributes->setInt(messages::kInputConnected,
                                           input_connected ? 1 : 0));
      static_cast<void>(attributes->setInt(messages::kOutputConnected,
                                           output_connected ? 1 : 0));
      static_cast<void>(sendMessage(message));
    }
  }
}

void Processor::SendAudioStatus(const audio::Status status,
                                const std::string& error) {
  if (const auto message = Steinberg::owned(allocateMessage())) {
    message->setMessageID(messages::kAudioStatus);
    if (auto* const attributes = message->getAttributes(); attributes != nullptr) {
      static_cast<void>(attributes->setInt(
          messages::kStatus, static_cast<Steinberg::int64>(status)));
      if (!error.empty()) {
        static_cast<void>(attributes->setBinary(
            messages::kError, error.data(),
            static_cast<Steinberg::uint32>(error.size())));
      }
      static_cast<void>(sendMessage(message));
    }
  }
}

auto PLUGIN_API Processor::getState(IBStream* const state) -> tresult {
  return state::Write(state, CurrentState()) ? kResultTrue : kResultFalse;
}

auto PLUGIN_API Processor::setState(IBStream* const state) -> tresult {
  auto data = state::Data{};
  if (!state::Read(state, data)) {
    return kResultFalse;
  }
  ApplyParameterState(data);
  ConfigureRouting(data, false);
  return kResultTrue;
}

auto Processor::CurrentState() -> state::Data {
  auto data = state::Data{};
  data.input_gain_db = input_gain_db_.load(std::memory_order_relaxed);
  data.output_gain_db = output_gain_db_.load(std::memory_order_relaxed);
  data.output_delay_ms = output_delay_ms_.load(std::memory_order_relaxed);
  data.exclusive = exclusive_.load(std::memory_order_relaxed);
  const auto lock = std::lock_guard(state_mutex_);
  if (config_.has_value()) {
    data.input_id = config_->input_id;
    data.output_id = config_->output_id;
  }
  return data;
}

void Processor::ApplyParameterState(const state::Data& data) {
  const auto sanitized = state::Sanitize(data);
  input_gain_db_.store(sanitized.input_gain_db, std::memory_order_relaxed);
  output_gain_db_.store(sanitized.output_gain_db, std::memory_order_relaxed);
  output_delay_ms_.store(sanitized.output_delay_ms,
                         std::memory_order_relaxed);
  exclusive_.store(sanitized.exclusive, std::memory_order_relaxed);
}

void Processor::ConfigureRouting(const state::Data& data,
                                 const bool restart_io) {
  auto config = audio::Config{data.input_id, data.output_id, data.exclusive,
                              sample_rate_};
  exclusive_.store(config.exclusive, std::memory_order_relaxed);
  {
    const auto lock = std::lock_guard(state_mutex_);
    if (config.input_id.empty() && config.output_id.empty()) {
      config_.reset();
    } else {
      config_ = config;
    }
  }
  if (!restart_io) {
    return;
  }
  if (config.input_id.empty() && config.output_id.empty()) {
    audio_io_.Stop();
  } else if (sample_rate_ > 0.0) {
    audio_io_.Start(config);
  }
  reset_output_delay_.store(true, std::memory_order_release);
}

auto PLUGIN_API Processor::notify(IMessage* const message) -> tresult {
  if (message == nullptr || message->getAttributes() == nullptr) {
    return kResultFalse;
  }
  const auto* const id = message->getMessageID();
  if (id == nullptr) {
    return kResultFalse;
  }
  auto* const attributes = message->getAttributes();
  const auto is_parameter_sync =
      std::strcmp(id, messages::kParameterSync) == 0;
  const auto is_state_sync = std::strcmp(id, messages::kStateSync) == 0;
  if (is_parameter_sync || is_state_sync) {
    auto data = state::Data{};
    if (!messages::ReadState(attributes, data, is_state_sync)) {
      return kResultFalse;
    }
    ApplyParameterState(data);
    if (is_state_sync) {
      ConfigureRouting(data, true);
    }
    return kResultTrue;
  }
  if (std::strcmp(id, messages::kAudioOff) == 0) {
    {
      const auto lock = std::lock_guard(state_mutex_);
      config_.reset();
    }
    audio_io_.Stop();
    reset_output_delay_.store(true, std::memory_order_release);
    return kResultTrue;
  }
  if (std::strcmp(id, messages::kAudioConfig) == 0) {
    auto data = state::Data{};
    if (!messages::ReadRouting(attributes, data.input_id, data.output_id,
                               data.exclusive)) {
      return kResultFalse;
    }
    ConfigureRouting(data, true);
    return kResultTrue;
  }
  return AudioEffect::notify(message);
}

}  // namespace audio_bridge::vst
