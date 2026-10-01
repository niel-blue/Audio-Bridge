#include "editor.h"

#include "audio_device_catalog.h"
#include "audio_device_ids.h"
#include "application_input.h"
#include "controller.h"
#include "editor_widgets.h"
#include "parameters.h"
#include "vst_messages.h"

#include <algorithm>
#include <string>

#include "pluginterfaces/base/smartpointer.h"
#include "vstgui4/vstgui/lib/cfont.h"
#include "vstgui4/vstgui/lib/cframe.h"
#include "vstgui4/vstgui/lib/cgradient.h"
#include "vstgui4/vstgui/lib/cviewcontainer.h"
#include "vstgui4/vstgui/lib/cvstguitimer.h"

namespace audio_bridge::vst {
Editor::Editor(void* const controller)
    : VSTGUIEditor(controller),
      font_(new VSTGUI::CFontDesc("Segoe UI", 14.0)),
      small_font_(new VSTGUI::CFontDesc("Segoe UI", 10.0)),
      heading_font_(new VSTGUI::CFontDesc("Segoe UI", 12.0,
                                          VSTGUI::kBoldFace)),
      strong_font_(new VSTGUI::CFontDesc("Segoe UI", 17.0,
                                         VSTGUI::kBoldFace)) {
  setRect(Steinberg::ViewRect(0, 0, 320, 330));
}

Editor::~Editor() {
  if (font_ != nullptr) font_->forget();
  if (small_font_ != nullptr) small_font_->forget();
  if (heading_font_ != nullptr) heading_font_->forget();
  if (strong_font_ != nullptr) strong_font_->forget();
}

auto PLUGIN_API Editor::open(void* const parent,
                             const PlatformType& /*platformType*/) -> bool {
  if (frame != nullptr) {
    return false;
  }
  BuildFrame(parent);
  return frame != nullptr;
}

void Editor::BuildFrame(void* const parent) {
  const auto window = VSTGUI::CRect(0, 0, 320, 330);
  constexpr auto kContentOffset = 3.0;
  frame = new VSTGUI::CFrame(window, this);
  if (frame == nullptr) {
    return;
  }
  frame->setBackgroundColor(ui::palette::kBackground);
  auto* const root = new VSTGUI::CViewContainer(window);
  root->setBackgroundColor(ui::palette::kBackground);
  root->setTransparency(false);
  frame->addView(root);

  root->addView(ui::MakeLabel(VSTGUI::CRect(20, 10, 139, 34),
                              "Audio-Bridge", strong_font_,
                              ui::palette::kText));
  root->addView(ui::MakeLabel(VSTGUI::CRect(151, 14, 302, 30),
                              "MULTI-BACKEND AUDIO ROUTER", small_font_,
                              ui::palette::kMuted));

  auto* const input_panel = new VSTGUI::CViewContainer(
      VSTGUI::CRect(0, 31 + kContentOffset, 320, 143 + kContentOffset));
  auto* const output_panel = new VSTGUI::CViewContainer(
      VSTGUI::CRect(0, 145 + kContentOffset, 320, 292 + kContentOffset));
  input_panel->setBackgroundColor(ui::palette::kBackground);
  output_panel->setBackgroundColor(ui::palette::kBackground);
  root->addView(input_panel);
  root->addView(output_panel);

  input_menu_ = ui::AddDeviceMenu(input_panel, this, "ADDITIONAL INPUT",
                                  heading_font_, small_font_);
  input_refresh_ = new VSTGUI::CTextButton(
      VSTGUI::CRect(246, 4, 302, 19), this, -1, "REFRESH");
  input_refresh_->setFont(small_font_);
  input_refresh_->setTextColor(ui::palette::kText);
  input_refresh_->setTextColorHighlighted(ui::palette::kText);
  input_refresh_->setFrameColor(ui::palette::kAccentDim);
  input_refresh_->setFrameColorHighlighted(ui::palette::kAccent);
  auto* const refresh_gradient = VSTGUI::CGradient::create(
      0, 1, ui::palette::kPanelRaised, ui::palette::kAccentDim);
  input_refresh_->setGradient(refresh_gradient);
  input_refresh_->setGradientHighlighted(refresh_gradient);
  refresh_gradient->forget();
  input_refresh_->setRoundRadius(2.0);
  input_refresh_->setFrameWidth(1.0);
  input_refresh_->setTextMargin(1.0);
  input_panel->addView(input_refresh_);
  output_menu_ = ui::AddDeviceMenu(output_panel, this, "ADDITIONAL OUTPUT",
                                   heading_font_, small_font_);
  input_meter_ = ui::AddLevelMeter(input_panel, 52);
  output_meter_ = ui::AddLevelMeter(output_panel, 52);

  input_panel->addView(ui::MakeLabel(VSTGUI::CRect(20, 63, 150, 75),
                                     "INPUT GAIN", small_font_,
                                     ui::palette::kMuted));
  output_panel->addView(ui::MakeLabel(VSTGUI::CRect(20, 63, 150, 75),
                                      "OUTPUT GAIN", small_font_,
                                      ui::palette::kMuted));

  const auto input_gain = ui::AddParameterControl(
      input_panel, this, small_font_, static_cast<int>(kInputGain), 76,
      static_cast<float>(kMinimumGainDb),
      static_cast<float>(kMaximumGainDb), 0.0F, 1.0F, "0.0 dB");
  input_gain_ = input_gain.slider;
  input_reset_ = input_gain.reset;
  input_gain_edit_ = input_gain.edit;

  const auto output_gain = ui::AddParameterControl(
      output_panel, this, small_font_, static_cast<int>(kOutputGain), 76,
      static_cast<float>(kMinimumGainDb),
      static_cast<float>(kMaximumGainDb), 0.0F, 1.0F, "0.0 dB");
  output_gain_ = output_gain.slider;
  output_reset_ = output_gain.reset;
  output_gain_edit_ = output_gain.edit;

  output_panel->addView(ui::MakeLabel(VSTGUI::CRect(20, 103, 150, 115),
                                      "OUTPUT DELAY", small_font_,
                                      ui::palette::kMuted));
  const auto output_delay = ui::AddParameterControl(
      output_panel, this, small_font_, static_cast<int>(kOutputDelay), 116,
      0.0F, static_cast<float>(kMaximumOutputDelayMs), 0.0F, 10.0F,
      "0.0 ms");
  output_delay_ = output_delay.slider;
  output_delay_reset_ = output_delay.reset;
  output_delay_edit_ = output_delay.edit;

  exclusive_checkbox_ = new VSTGUI::CCheckBox(
      VSTGUI::CRect(20, 296 + kContentOffset, 154, 322 + kContentOffset),
      this, static_cast<int>(kExclusive), "WASAPI EXCLUSIVE");
  exclusive_checkbox_->setFont(small_font_);
  exclusive_checkbox_->setFontColor(ui::palette::kText);
  exclusive_checkbox_->setBoxFrameColor(ui::palette::kBorder);
  exclusive_checkbox_->setBoxFillColor(ui::palette::kPanelRaised);
  exclusive_checkbox_->setCheckMarkColor(ui::palette::kAccent);
  root->addView(exclusive_checkbox_);

  status_label_ = ui::MakeLabel(
      VSTGUI::CRect(153, 296 + kContentOffset, 302, 322 + kContentOffset),
      "●  OFF  /  Select a device to start", small_font_,
      ui::palette::kMuted);
  status_label_->setHoriAlign(VSTGUI::CHoriTxtAlign::kRightText);
  root->addView(status_label_);

  RefreshDevices();
  timer_ = VSTGUI::owned(new VSTGUI::CVSTGUITimer(
      [this](VSTGUI::CVSTGUITimer*) { PollState(); }, 50));
  if (!frame->open(parent)) {
    frame->forget();
    frame = nullptr;
  }
}

void PLUGIN_API Editor::close() {
  if (timer_) {
    timer_->stop();
    timer_ = nullptr;
  }
  if (frame != nullptr) {
    frame->forget();
    frame = nullptr;
  }
  input_menu_ = nullptr;
  output_menu_ = nullptr;
  input_refresh_ = nullptr;
  exclusive_checkbox_ = nullptr;
  input_gain_ = nullptr;
  output_gain_ = nullptr;
  output_delay_ = nullptr;
  input_reset_ = nullptr;
  output_reset_ = nullptr;
  output_delay_reset_ = nullptr;
  input_gain_edit_ = nullptr;
  output_gain_edit_ = nullptr;
  output_delay_edit_ = nullptr;
  status_label_ = nullptr;
  input_meter_ = nullptr;
  output_meter_ = nullptr;
  input_ids_.clear();
  output_ids_.clear();
}

void Editor::UpdateHostRouteLabels(const bool input_connected,
                                   const bool output_connected) {
  const auto update = [](VSTGUI::COptionMenu* const menu,
                         const bool /*connected*/, const char* const /*host_label*/) {
    if (menu == nullptr) {
      return;
    }
    if (auto* const host_entry = menu->getEntry(0); host_entry != nullptr) {
      host_entry->setTitle("OFF");
    }
    menu->invalid();
  };
  update(input_menu_, input_connected, "DAW / HOST INPUT");
  update(output_menu_, output_connected, "DAW / HOST OUTPUT");
}

void Editor::RefreshDevices() {
  if (input_menu_ == nullptr || output_menu_ == nullptr) {
    return;
  }
  input_menu_->removeAllEntry();
  output_menu_->removeAllEntry();
  input_menu_->addEntry("OFF");
  output_menu_->addEntry("OFF");
  input_ids_.clear();
  output_ids_.clear();
  auto* const controller = static_cast<Controller*>(getController());
  if (controller == nullptr) {
    return;
  }
  auto input_id = std::string{};
  auto output_id = std::string{};
  auto exclusive = false;
  auto host_input_connected = false;
  auto host_output_connected = false;
  controller->GetDeviceSelection(input_id, output_id, exclusive);
  controller->GetHostRoutes(host_input_connected, host_output_connected);

  auto applications =
      audio::EnumerateApplicationInputs(audio::CurrentApplicationInputExclusions());
  if (device_ids::IsApplicationInput(input_id)) {
    applications = audio::MergeRememberedApplicationInput(
        std::move(applications), device_ids::ApplicationInputPayload(input_id),
        audio::CurrentApplicationInputExclusions());
  }
  for (const auto& application : applications.applications) {
    input_ids_.push_back(
        device_ids::MakeApplicationInputId(application.identity));
    auto name = std::string("[Application Input] ") +
                application.display_name;
    if (application.process_id == 0) {
      name += "  (Not running)";
    }
    input_menu_->addEntry(name.c_str());
  }
  const auto snapshot = EnumerateAudioDevices();
  for (const auto& device : snapshot.inputs) {
    input_ids_.push_back(device.id);
    auto name = std::string("[") + DeviceBackendLabel(device.backend) + "] " +
                device.name + (device.is_default ? "  (Default)" : "");
    input_menu_->addEntry(name.c_str());
  }
  for (const auto& device : snapshot.outputs) {
    output_ids_.push_back(device.id);
    auto name = std::string("[") + DeviceBackendLabel(device.backend) + "] " +
                device.name + (device.is_default ? "  (Default)" : "");
    output_menu_->addEntry(name.c_str());
  }
  UpdateHostRouteLabels(host_input_connected, host_output_connected);
  auto find_index = [](const std::vector<std::string>& ids,
                       const std::string& id) {
    auto it = std::find(ids.begin(), ids.end(), id);
    if (it == ids.end() && !id.empty() && !device_ids::IsLegacy(id) &&
        !device_ids::IsWasapi(id) && !device_ids::IsApplicationInput(id)) {
      it = std::find(ids.begin(), ids.end(),
                     std::string(device_ids::kWasapiPrefix) + id);
    }
    return it == ids.end() ? 0 : static_cast<int>(it - ids.begin()) + 1;
  };
  static_cast<void>(input_menu_->setCurrent(find_index(input_ids_, input_id)));
  static_cast<void>(output_menu_->setCurrent(find_index(output_ids_, output_id)));
  if (exclusive_checkbox_ != nullptr) {
    exclusive_checkbox_->setValue(exclusive ? 1.0F : 0.0F);
  }
  RefreshParameters();
}

void Editor::RefreshParameters() {
  auto* const controller = static_cast<Controller*>(getController());
  if (controller == nullptr) {
    return;
  }
  const auto update_slider_and_text =
      [controller](VSTGUI::CHorizontalSlider* const slider,
                   VSTGUI::CTextEdit* const edit, const ParamID id,
                   const bool is_delay) {
        if (slider == nullptr || slider->isEditing()) {
          return;
        }
        slider->setValueNormalized(static_cast<float>(
            std::clamp(controller->getParamNormalized(id), 0.0, 1.0)));
        if (edit != nullptr &&
            (edit->getFrame() == nullptr ||
             edit->getFrame()->getFocusView() != edit)) {
          if (is_delay) {
            const auto value = static_cast<double>(slider->getValue());
            edit->setText(ui::FormatMilliseconds(value).c_str());
          } else {
            edit->setText(ui::FormatDb(static_cast<double>(slider->getValue()))
                              .c_str());
          }
          edit->invalid();
        }
      };
  update_slider_and_text(input_gain_, input_gain_edit_, kInputGain, false);
  update_slider_and_text(output_gain_, output_gain_edit_, kOutputGain, false);
  update_slider_and_text(output_delay_, output_delay_edit_, kOutputDelay, true);
  if (exclusive_checkbox_ != nullptr && !exclusive_checkbox_->isEditing()) {
    exclusive_checkbox_->setValue(static_cast<float>(std::clamp(
        controller->getParamNormalized(kExclusive), 0.0, 1.0)));
  }
}

void Editor::SendDeviceConfig() {
  auto* const controller = static_cast<Controller*>(getController());
  if (controller == nullptr || input_menu_ == nullptr || output_menu_ == nullptr) {
    return;
  }
  const auto input_index = input_menu_->getCurrentIndex();
  const auto output_index = output_menu_->getCurrentIndex();
  const auto input_id = input_index > 0 &&
                                input_index <= static_cast<int>(input_ids_.size())
                            ? input_ids_[input_index - 1]
                            : std::string{};
  const auto output_id = output_index > 0 &&
                                  output_index <= static_cast<int>(output_ids_.size())
                              ? output_ids_[output_index - 1]
                              : std::string{};
  const auto exclusive = exclusive_checkbox_ != nullptr &&
                         exclusive_checkbox_->getValue() > 0.5F;
  controller->SetDeviceSelection(input_id, output_id,
                                 (!input_id.empty() || !output_id.empty())
                                     ? exclusive
                                     : false);
  static_cast<void>(controller->setDirty(true));
  if (const auto message = Steinberg::owned(controller->allocateMessage())) {
    const auto external_route = !input_id.empty() || !output_id.empty();
    message->setMessageID(external_route ? messages::kAudioConfig
                                         : messages::kAudioOff);
    if (!external_route ||
        messages::WriteRouting(message->getAttributes(), input_id, output_id,
                               exclusive)) {
      static_cast<void>(controller->sendMessage(message));
    }
  }
}

void Editor::SendParameterEdit(const ParamID id, const double normalized) {
  auto* const controller = static_cast<Controller*>(getController());
  if (controller == nullptr) {
    return;
  }
  if (controller->setParamNormalized(id, normalized) == Steinberg::kResultTrue) {
    controller->performEdit(id, controller->getParamNormalized(id));
    controller->SyncParameterStateToProcessor();
  }
}

void Editor::SetGainText(VSTGUI::CTextEdit* const edit, const double value) {
  if (edit != nullptr) {
    edit->setText(ui::FormatDb(value).c_str());
    edit->invalid();
  }
}

void Editor::SetDelayText(VSTGUI::CTextEdit* const edit, const double value) {
  if (edit != nullptr) {
    edit->setText(ui::FormatMilliseconds(value).c_str());
    edit->invalid();
  }
}

void Editor::UpdateStatus() {
  auto* const controller = static_cast<Controller*>(getController());
  if (controller == nullptr || status_label_ == nullptr) {
    return;
  }
  audio::Status status = audio::Status::kOff;
  auto error = std::string{};
  auto host_input_connected = false;
  auto host_output_connected = false;
  controller->GetAudioStatus(status, error);
  controller->GetHostRoutes(host_input_connected, host_output_connected);
  const auto host_route_connected =
      host_input_connected || host_output_connected;
  const auto text = status == audio::Status::kStarting
                        ? "●  STARTING"
                        : (status == audio::Status::kActive
                               ? "●  ACTIVE"
                               : (status == audio::Status::kUnavailable
                                       ? "●  UNAVAILABLE  " + error
                                       : (host_route_connected
                                              ? "●  DAW / HOST"
                                              : "●  OFF  /  Select a device to start")));
  status_label_->setText(text.c_str());
  status_label_->setFontColor(
      status == audio::Status::kUnavailable
          ? ui::palette::kDanger
          : (status == audio::Status::kActive ? ui::palette::kAccent
                                               : ui::palette::kMuted));
  status_label_->invalid();
}

void Editor::PollState() {
  auto* const controller = static_cast<Controller*>(getController());
  if (controller == nullptr) {
    return;
  }
  float input_peak = 0.0F;
  float output_peak = 0.0F;
  controller->GetAudioLevels(input_peak, output_peak);
  auto input_id = std::string{};
  auto output_id = std::string{};
  auto exclusive = false;
  auto host_input_connected = false;
  auto host_output_connected = false;
  controller->GetDeviceSelection(input_id, output_id, exclusive);
  controller->GetHostRoutes(host_input_connected, host_output_connected);
  UpdateHostRouteLabels(host_input_connected, host_output_connected);
  if (input_meter_ != nullptr) {
    if (input_id.empty() && !host_input_connected) {
      input_meter_->Clear();
    } else {
      input_meter_->SetPeak(input_peak);
    }
  }
  if (output_meter_ != nullptr) {
    if (output_id.empty() && !host_output_connected) {
      output_meter_->Clear();
    } else {
      output_meter_->SetPeak(output_peak);
    }
  }
  RefreshParameters();
  UpdateStatus();
}

void Editor::valueChanged(CControl* const control) {
  if (control == nullptr) {
    return;
  }
  if (control == input_menu_ || control == output_menu_) {
    SendDeviceConfig();
    return;
  }
  if (control == input_refresh_) {
    RefreshDevices();
    SendDeviceConfig();
    return;
  }
  if (control == exclusive_checkbox_) {
    SendParameterEdit(kExclusive, exclusive_checkbox_->getValue() > 0.5F ? 1.0
                                                                      : 0.0);
    SendDeviceConfig();
    return;
  }
  if (control == input_reset_ || control == output_reset_) {
    const auto id = control == input_reset_ ? kInputGain : kOutputGain;
    auto* const slider = control == input_reset_ ? input_gain_ : output_gain_;
    SendParameterEdit(id, GainDbToNormalized(0.0));
    if (slider != nullptr) {
      slider->setValue(0.0F);
    }
    SetGainText(control == input_reset_ ? input_gain_edit_ : output_gain_edit_,
                0.0);
    return;
  }
  if (control == output_delay_reset_) {
    SendParameterEdit(kOutputDelay, DelayMsToNormalized(0.0));
    if (output_delay_ != nullptr) {
      output_delay_->setValue(0.0F);
    }
    SetDelayText(output_delay_edit_, 0.0);
    return;
  }
  if (control == input_gain_edit_ || control == output_gain_edit_) {
    auto* const edit = control == input_gain_edit_ ? input_gain_edit_
                                                    : output_gain_edit_;
    auto* const slider = control == input_gain_edit_ ? input_gain_
                                                       : output_gain_;
    const auto id = control == input_gain_edit_ ? kInputGain : kOutputGain;
    auto value = 0.0;
    if (!ui::ParseValue(edit->getText().getString(), "dB", kMinimumGainDb,
                        kMaximumGainDb, value)) {
      value = slider != nullptr ? slider->getValue() : 0.0;
    }
    if (slider != nullptr) {
      slider->setValue(static_cast<float>(value));
    }
    SetGainText(edit, value);
    SendParameterEdit(id, GainDbToNormalized(value));
    return;
  }
  if (control == output_delay_edit_) {
    auto value = 0.0;
    if (!ui::ParseValue(output_delay_edit_->getText().getString(), "ms", 0.0,
                        kMaximumOutputDelayMs, value)) {
      value = output_delay_ != nullptr ? output_delay_->getValue() : 0.0;
    }
    if (output_delay_ != nullptr) {
      output_delay_->setValue(static_cast<float>(value));
    }
    SetDelayText(output_delay_edit_, value);
    SendParameterEdit(kOutputDelay, DelayMsToNormalized(value));
    return;
  }
  if (control == input_gain_ || control == output_gain_) {
    auto* const edit = control == input_gain_ ? input_gain_edit_
                                               : output_gain_edit_;
    SetGainText(edit, control->getValue());
    SendParameterEdit(static_cast<ParamID>(control->getTag()),
                      control->getValueNormalized());
    return;
  }
  if (control == output_delay_) {
    SetDelayText(output_delay_edit_, control->getValue());
    SendParameterEdit(kOutputDelay, control->getValueNormalized());
    return;
  }
}

}  // namespace audio_bridge::vst
