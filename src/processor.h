#ifndef AUDIO_BRIDGE_PROCESSOR_H_
#define AUDIO_BRIDGE_PROCESSOR_H

#include <atomic>
#include <cstddef>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/vsttypes.h"
#include "public.sdk/source/vst/vstaudioeffect.h"

#include "audio_io.h"
#include "io_state.h"
#include "output_delay.h"

namespace audio_bridge::vst {

class Processor final : public Steinberg::Vst::AudioEffect {
  using tresult = Steinberg::tresult;
  using int32 = Steinberg::int32;
  using uint32 = Steinberg::uint32;
  using TBool = Steinberg::TBool;
  using IBStream = Steinberg::IBStream;
  using SpeakerArrangement = Steinberg::Vst::SpeakerArrangement;
  using ProcessSetup = Steinberg::Vst::ProcessSetup;
  using ProcessData = Steinberg::Vst::ProcessData;
  using IMessage = Steinberg::Vst::IMessage;
  using ParamID = Steinberg::Vst::ParamID;
  using ParamValue = Steinberg::Vst::ParamValue;

 public:
  Processor();
  ~Processor() override;

  auto PLUGIN_API initialize(Steinberg::FUnknown* context)
      -> tresult SMTG_OVERRIDE;
  auto PLUGIN_API setBusArrangements(SpeakerArrangement* inputs, int32 numIns,
                                     SpeakerArrangement* outputs, int32 numOuts)
      -> tresult SMTG_OVERRIDE;
  auto PLUGIN_API setupProcessing(ProcessSetup& setup)
      -> tresult SMTG_OVERRIDE;
  auto PLUGIN_API getLatencySamples() -> uint32 SMTG_OVERRIDE;
  auto PLUGIN_API setActive(TBool state) -> tresult SMTG_OVERRIDE;
  auto PLUGIN_API process(ProcessData& data) -> tresult SMTG_OVERRIDE;
  auto PLUGIN_API getState(IBStream* state) -> tresult SMTG_OVERRIDE;
  auto PLUGIN_API setState(IBStream* state) -> tresult SMTG_OVERRIDE;
  auto PLUGIN_API notify(IMessage* message) -> tresult SMTG_OVERRIDE;

  // NOLINTNEXTLINE(readability-identifier-naming)
  static auto createInstance(void*) -> Steinberg::FUnknown* {
    return static_cast<Steinberg::Vst::IAudioProcessor*>(new Processor());
  }

 private:
  void StartConfiguredIo();
  void SendAudioLevels();
  void SendAudioStatus(audio::Status status, const std::string& error);
  void SendHostRoutes(bool input_connected, bool output_connected);
  auto CurrentState() -> state::Data;
  void ApplyParameterState(const state::Data& data);
  void ConfigureRouting(const state::Data& data, bool restart_io);

  std::mutex state_mutex_;
  audio::AudioIo audio_io_;
  std::optional<audio::Config> config_;
  std::atomic<double> input_gain_db_{0.0};
  std::atomic<double> output_gain_db_{0.0};
  std::atomic<double> output_delay_ms_{0.0};
  std::atomic<bool> exclusive_{false};
  double sample_rate_ = 0.0;
  std::int64_t meter_frames_ = 0;
  float input_peak_ = 0.0F;
  float output_peak_ = 0.0F;
  std::vector<float> direct_input_left_;
  std::vector<float> direct_input_right_;
  std::vector<float> output_left_;
  std::vector<float> output_right_;
  audio::OutputDelay output_delay_;
  std::atomic<bool> reset_output_delay_{false};
  bool host_input_connected_ = false;
  bool host_output_connected_ = false;
};

}  // namespace audio_bridge::vst

#endif
