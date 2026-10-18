#ifndef AUDIO_BRIDGE_CONTROLLER_H_
#define AUDIO_BRIDGE_CONTROLLER_H

#include <atomic>
#include <mutex>
#include <string>

#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "public.sdk/source/vst/vsteditcontroller.h"

#include "audio_io.h"
#include "io_state.h"

namespace audio_bridge::vst {

class Editor;

class Controller final : public Steinberg::Vst::EditController {
  using tresult = Steinberg::tresult;
  using ParamID = Steinberg::Vst::ParamID;
  using ParamValue = Steinberg::Vst::ParamValue;
  using IBStream = Steinberg::IBStream;
  using IMessage = Steinberg::Vst::IMessage;

 public:
  ~Controller() override;

  auto PLUGIN_API initialize(Steinberg::FUnknown* context)
      -> tresult SMTG_OVERRIDE;
  auto PLUGIN_API setComponentState(IBStream* state)
      -> tresult SMTG_OVERRIDE;
  auto PLUGIN_API setState(IBStream* state) -> tresult SMTG_OVERRIDE;
  auto PLUGIN_API getState(IBStream* state) -> tresult SMTG_OVERRIDE;
  auto PLUGIN_API createView(const char* name)
      -> Steinberg::IPlugView* SMTG_OVERRIDE;
  auto PLUGIN_API notify(IMessage* message) -> tresult SMTG_OVERRIDE;

  auto PLUGIN_API setParamNormalized(ParamID id, ParamValue value) -> tresult SMTG_OVERRIDE;

  void SetDeviceSelection(std::string input_id, std::string output_id,
                          bool exclusive);
  void SyncParameterStateToProcessor();
  void GetDeviceSelection(std::string& input_id, std::string& output_id,
                          bool& exclusive) const;
  void GetHostRoutes(bool& input_connected, bool& output_connected) const;
  void GetAudioLevels(float& input_peak, float& output_peak) const;
  void GetAudioStatus(audio::Status& status, std::string& error) const;

  // NOLINTNEXTLINE(readability-identifier-naming)
  static auto createInstance(void*) -> Steinberg::FUnknown* {
    return static_cast<Steinberg::Vst::IEditController*>(new Controller());
  }

 private:
  auto CurrentState() -> state::Data;
  void ApplyState(const state::Data& data);
  void SendStateToProcessor(const state::Data& data, bool include_devices);

  mutable std::mutex device_mutex_;
  std::string input_id_;
  std::string output_id_;
  bool exclusive_ = false;
  std::atomic<float> input_peak_{0.0F};
  std::atomic<float> output_peak_{0.0F};
  std::atomic<bool> host_input_connected_{false};
  std::atomic<bool> host_output_connected_{false};
  std::atomic<audio::Status> status_{audio::Status::kOff};
  std::string status_error_;
};

}  // namespace audio_bridge::vst

#endif
