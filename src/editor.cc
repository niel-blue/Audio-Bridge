#include "editor.h"

#include "audio_device_catalog.h"
#include "audio_device_ids.h"
#include "application_input.h"
#include "controller.h"
#include "editor_widgets.h"
#include "parameters.h"
#include "vst_messages.h"

#include <algorithm>
#include <array>
#include <string>

#include "pluginterfaces/base/smartpointer.h"
#include "vstgui4/vstgui/lib/cfont.h"
#include "vstgui4/vstgui/lib/cframe.h"
#include "vstgui4/vstgui/lib/cgradient.h"
#include "vstgui4/vstgui/lib/cviewcontainer.h"
#include "vstgui4/vstgui/lib/cvstguitimer.h"

namespace audio_bridge::vst {
namespace {
constexpr double kBaseEditorWidth = 300.0;
constexpr double kBaseEditorHeight = 380.0;
// Update cached standard-control colors in place; keep device lists, edits,
// audio parameters and ongoing control values intact.
void Recolor(VSTGUI::CView* view, const ui::Palette& old, const ui::Palette& next) {
  const std::array<const VSTGUI::CColor*,12> from {&old.kBackground,&old.kPanelRaised,
    &old.kControlBackground,&old.kSliderTrack,&old.kBorder,&old.kAccent,
    &old.kButtonBorder,&old.kReset,&old.kText,&old.kMuted,&old.kDanger,&old.kResetPressed};
  const std::array<const VSTGUI::CColor*,12> to {&next.kBackground,&next.kPanelRaised,
    &next.kControlBackground,&next.kSliderTrack,&next.kBorder,&next.kAccent,
    &next.kButtonBorder,&next.kReset,&next.kText,&next.kMuted,&next.kDanger,&next.kResetPressed};
  const auto map = [&](VSTGUI::CColor c) {
    for (size_t i=0;i<from.size();++i) if(c==*from[i]) return *to[i];
    return c;
  };
  if(auto* display=dynamic_cast<VSTGUI::CParamDisplay*>(view)) {
    display->setFontColor(map(display->getFontColor()));
    display->setBackColor(map(display->getBackColor()));
    // Border and buttonBorder share a Light RGB value but differ in Dark.
    // Restore semantic roles instead of trying to infer them from that RGB.
    if (auto* edit=dynamic_cast<VSTGUI::CTextEdit*>(display);
        edit && !(edit->getStyle() & VSTGUI::CParamDisplay::kNoFrame))
      display->setFrameColor(next.kButtonBorder);
    else display->setFrameColor(map(display->getFrameColor()));
  }
  if(auto* button=dynamic_cast<VSTGUI::CTextButton*>(view)) {
    button->setTextColor(next.kText); button->setTextColorHighlighted(next.kPressedText);
    button->setFrameColor(next.kAccent); button->setFrameColorHighlighted(next.kAccent);
    auto* normal=VSTGUI::CGradient::create(0,1,next.kBackground,next.kBackground);
    auto* pressed=VSTGUI::CGradient::create(0,1,next.kReset,next.kReset);
    button->setGradient(normal);button->setGradientHighlighted(pressed);
    normal->forget();pressed->forget();
  }
  if(auto* check=dynamic_cast<VSTGUI::CCheckBox*>(view)) {
    check->setFontColor(next.kText);check->setBoxFrameColor(next.kBorder);
    check->setBoxFillColor(next.kPanelRaised);check->setCheckMarkColor(next.kAccent);
  }
  if(auto* container=dynamic_cast<VSTGUI::CViewContainer*>(view)) {
    container->setBackgroundColor(map(container->getBackgroundColor()));
    for(uint32_t i=0;i<container->getNbViews();++i) Recolor(container->getView(i),old,next);
  }
  view->invalid();
}

void StyleThemeButton(VSTGUI::CTextButton* button, const ui::Palette& colors) {
  if (!button) return;
  const auto& palette = colors.light ? vst_base::kLight : vst_base::kDark;
  const auto color = [](vst_base::Color c) { return VSTGUI::CColor(c.r,c.g,c.b); };
  const auto fill = color(colors.light ? palette.header : palette.surface);
  button->setFrameWidth(0);
  button->setFrameColor(VSTGUI::kTransparentCColor);
  button->setFrameColorHighlighted(VSTGUI::kTransparentCColor);
  button->setTextColor(colors.kMuted);
  button->setTextColorHighlighted(colors.kText);
  auto* normal = VSTGUI::CGradient::create(0,1,fill,fill);
  auto* pressed = VSTGUI::CGradient::create(0,1,colors.kPanelRaised,colors.kPanelRaised);
  button->setGradient(normal);
  button->setGradientHighlighted(pressed);
  normal->forget();
  pressed->forget();
}
}  // namespace

Editor::Editor(void* const controller)
    : VSTGUIEditor(controller),
      font_(new VSTGUI::CFontDesc("Segoe UI", 14.0)),
      small_font_(new VSTGUI::CFontDesc("Segoe UI", 10.0)),
      heading_font_(new VSTGUI::CFontDesc("Segoe UI", 12.0,
                                          VSTGUI::kBoldFace)),
      strong_font_(new VSTGUI::CFontDesc("Segoe UI", 17.0,
                                         VSTGUI::kBoldFace)) {
  auto* owner = static_cast<Controller*>(getController());
  colors_ = ui::Palette::From(owner->IsLightTheme());
  owner->SetEditor(this);
  setRect(Steinberg::ViewRect(0, 0, 300, 380));
}

Editor::~Editor() {
  if (frame) close();
  if (auto* owner = static_cast<Controller*>(getController())) owner->ClearEditor(this);
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

int32_t Editor::getKnobMode() const {
  return VSTGUI::kLinearMode;
}

void Editor::BuildFrame(void* const parent) {
  const auto window = VSTGUI::CRect(0, 0, kBaseEditorWidth, kBaseEditorHeight);
  frame = new VSTGUI::CFrame(window, this);
  frame->setBackgroundColor(colors_.kBackground);
  auto* root = new VSTGUI::CViewContainer(window);
  root->setBackgroundColor(colors_.kBackground);
  root->setTransparency(false);
  frame->addView(root);
  root->addView(ui::MakeLabel(VSTGUI::CRect(10,1,140,29), "Audio-Bridge", strong_font_, colors_.kText));
  auto* compact_font = new VSTGUI::CFontDesc("Segoe UI", 8);
  auto button = [&](VSTGUI::CViewContainer* parent, VSTGUI::CRect rect, const char* text, int tag, bool toggle=false) {
    auto* c = new VSTGUI::CTextButton(rect, this, tag, text,
        toggle ? VSTGUI::CTextButton::kOnOffStyle : VSTGUI::CTextButton::kKickStyle);
    c->setFont(compact_font); c->setTextColor(colors_.kText);
    c->setTextColorHighlighted(colors_.kPressedText);
    c->setFrameColor(colors_.kAccent); c->setFrameColorHighlighted(colors_.kAccent);
    auto* normal = VSTGUI::CGradient::create(0,1,colors_.kBackground,colors_.kBackground);
    auto* pressed = VSTGUI::CGradient::create(0,1,colors_.kReset,colors_.kReset);
    c->setGradient(normal); c->setGradientHighlighted(pressed); normal->forget(); pressed->forget();
    c->setRoundRadius(2); c->setFrameWidth(1); c->setTextMargin(0); parent->addView(c); return c;
  };
  theme_button_ = button(root,VSTGUI::CRect(250,7,290,23),
      colors_.light ? "Light" : "Dark",kThemeControlTag);
  StyleThemeButton(theme_button_,colors_);
  auto edit = [&](VSTGUI::CViewContainer* parent, VSTGUI::CRect rect, const char* text, bool framed=false) {
    auto* c = new VSTGUI::CTextEdit(rect, this, -1, text, nullptr,
        framed ? 0 : VSTGUI::CParamDisplay::kNoFrame);
    c->setFont(compact_font); c->setFontColor(colors_.kText);
    c->setBackColor(colors_.kBackground); c->setHoriAlign(VSTGUI::kCenterText);
    if (framed) {
      c->setFont(small_font_);
      c->setBackColor(colors_.kPanelRaised);
      c->setFrameColor(colors_.kButtonBorder);
      c->setTextInset(VSTGUI::CPoint(4,0));
    }
    c->setImmediateTextChange(false); parent->addView(c); return c;
  };
  for (int section=0; section<2; ++section) {
    auto& c = sections_[section]; const bool input = section==0;
    const double y = input ? 29 : 191;
    auto* panel = new VSTGUI::CViewContainer(VSTGUI::CRect(0,y,300,y+160));
    panel->setBackgroundColor(colors_.kBackground); root->addView(panel);
    auto* menu = ui::AddDeviceMenu(panel,this,input ? "INPUT" : "ADDITIONAL OUTPUT",heading_font_,small_font_,colors_);
    if(input) { input_menu_=menu; input_refresh_=button(panel,VSTGUI::CRect(240,4,290,18),"REFRESH",-1); }
    else { output_menu_=menu; output_menu_->removeAllEntry(); output_menu_->addEntry("HOST OUTPUT"); }
    auto* meter=ui::AddLevelMeter(panel,51,colors_);
    if(input) input_meter_=meter; else output_meter_=meter;
    // Framed panels, without adding a native child window or resize behavior.
    const auto box = [&](VSTGUI::CRect r) {
      auto* b = new VSTGUI::CTextLabel(r,"",nullptr,0);
      b->setBackColor(colors_.kBackground); b->setFrameColor(colors_.kBorder);
      b->setMouseEnabled(false); panel->addView(b);
    };
    box(VSTGUI::CRect(10,60,150,157)); box(VSTGUI::CRect(154,60,290,157));
    panel->addView(ui::MakeLabel(VSTGUI::CRect(14,62,106,75),input ? "INPUT GAIN" : "OUTPUT GAIN",small_font_,colors_.kMuted));
    c.reset=button(panel,VSTGUI::CRect(111,63,145,76),"RESET",-1);
    panel->addView(ui::MakeLabel(VSTGUI::CRect(158,62,251,75),input ? "INPUT DELAY" : "OUTPUT DELAY",small_font_,colors_.kMuted));
    c.delay_reset=button(panel,VSTGUI::CRect(252,63,286,76),"RESET",-1);
    c.link=new ui::LinkSwitch(VSTGUI::CRect(60,77,98,116),this,input ? kInputLink : kOutputLink,colors_);
    panel->addView(c.link);
    for(int ch=0;ch<2;++ch) {
      const double x=ch==0 ? 17 : 99;
      c.gain[ch]=new ui::RotaryKnob(VSTGUI::CRect(x,79,x+40,119),this,
          input ? (ch==0 ? kInputGain : kInputGainRight) : (ch==0 ? kOutputGain : kOutputGainRight),false,colors_);
      panel->addView(c.gain[ch]);
      auto* label=ui::MakeLabel(VSTGUI::CRect(x,119,x+40,129),ch==0 ? "L" : "R",compact_font,colors_.kText);
      label->setHoriAlign(VSTGUI::kCenterText); panel->addView(label);
      c.gain_edit[ch]=edit(panel,VSTGUI::CRect(x-4,129,x+44,141),"0.0 dB");
      c.mute[ch]=button(panel,VSTGUI::CRect(x-3,142,x+43,154),"MUTE",
          input ? (ch==0 ? kInputMuteLeft : kInputMuteRight) : (ch==0 ? kOutputMuteLeft : kOutputMuteRight),true);
    }
    c.delay=new ui::RotaryKnob(VSTGUI::CRect(195,80,249,134),this,input ? kInputDelay : kOutputDelay,true,colors_);
    panel->addView(c.delay);
    c.delay_edit=edit(panel,VSTGUI::CRect(186,135,258,153),"0.0 ms",true);
  }
  compact_font->forget();
  exclusive_checkbox_ = new VSTGUI::CCheckBox(VSTGUI::CRect(10,354,150,378),this,kExclusive,"WASAPI EXCLUSIVE");
  exclusive_checkbox_->setFont(small_font_); exclusive_checkbox_->setFontColor(colors_.kText);
  exclusive_checkbox_->setBoxFrameColor(colors_.kBorder); exclusive_checkbox_->setBoxFillColor(colors_.kPanelRaised);
  exclusive_checkbox_->setCheckMarkColor(colors_.kAccent); root->addView(exclusive_checkbox_);
  status_label_=ui::MakeLabel(VSTGUI::CRect(150,354,290,378),"●  OFF",small_font_,colors_.kMuted);
  status_label_->setHoriAlign(VSTGUI::kRightText); root->addView(status_label_);

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
  theme_button_ = nullptr;
  input_menu_ = nullptr;
  output_menu_ = nullptr;
  input_refresh_ = nullptr;
  exclusive_checkbox_ = nullptr;
  for (auto& section : sections_) section = {};
  status_label_ = nullptr;
  input_meter_ = nullptr;
  output_meter_ = nullptr;
  input_ids_.clear();
  output_ids_.clear();
}

void Editor::SetLightTheme(bool light, bool notify_host) {
  if (colors_.light == light) return;
  const auto old = colors_;
  colors_ = ui::Palette::From(light);
  if (frame) {
    Recolor(frame,old,colors_);
    if(theme_button_) {
      theme_button_->setTitle(light ? "Light" : "Dark");
      StyleThemeButton(theme_button_,colors_);
    }
    UpdateStatus();
  }
  if (auto* owner=static_cast<Controller*>(getController()))
    owner->SetLightTheme(light,notify_host);
}
void Editor::RefreshDevices() {
  if (input_menu_ == nullptr || output_menu_ == nullptr) {
    return;
  }
  input_menu_->removeAllEntry();
  output_menu_->removeAllEntry();
  input_menu_->addEntry("HOST INPUT");
  output_menu_->addEntry("HOST OUTPUT");
  input_ids_.clear();
  output_ids_.clear();
  auto* const controller = static_cast<Controller*>(getController());
  if (controller == nullptr) {
    return;
  }
  auto input_id = std::string{};
  auto output_id = std::string{};
  auto exclusive = false;
  controller->GetDeviceSelection(input_id, output_id, exclusive);

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
  const auto update = [&](ui::RotaryKnob* knob, VSTGUI::CTextEdit* edit, ParamID id, bool delay) {
    if (!knob || knob->isEditing()) return;
    const auto normalized = controller->getParamNormalized(id);
    const auto value = delay ? DelayMsFromNormalized(normalized) : GainDbFromNormalized(normalized);
    // The knob has a narrower range than typed/stored delay values.
    knob->setValue(static_cast<float>(std::clamp(value,
        static_cast<double>(knob->getMin()), static_cast<double>(knob->getMax()))));
    knob->invalid();
    if (edit && !edit->getPlatformTextEdit()) {
      edit->setText((delay ? ui::FormatMilliseconds(value) : ui::FormatDb(value)).c_str());
      edit->invalid();
    }
  };
  for(int section=0;section<2;++section) {
    auto& c=sections_[section]; const bool input=section==0;
    update(c.gain[0],c.gain_edit[0],input ? kInputGain : kOutputGain,false);
    update(c.gain[1],c.gain_edit[1],input ? kInputGainRight : kOutputGainRight,false);
    update(c.delay,c.delay_edit,input ? kInputDelay : kOutputDelay,true);
    // CTextButton stages its next value on mouse-down and commits on mouse-up.
    // Polling must not overwrite that staged value while a gesture is active.
    if (!c.link->isEditing()) {
      c.link->setValue(static_cast<float>(controller->getParamNormalized(input ? kInputLink : kOutputLink)));
      c.link->setTitle(c.link->getValue()>0.5F ? "LINK ON" : "LINK OFF");
      c.link->invalid();
    }
    for(int ch=0;ch<2;++ch) {
      if (c.mute[ch]->isEditing()) continue;
      c.mute[ch]->setValue(static_cast<float>(controller->getParamNormalized(input ? (ch==0 ? kInputMuteLeft : kInputMuteRight) : (ch==0 ? kOutputMuteLeft : kOutputMuteRight))));
      c.mute[ch]->invalid();
    }
  }
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
  bool active_gesture = false;
  for (const auto& section : sections_) {
    for (auto* knob : section.gain)
      active_gesture |= knob && knob->getTag()==static_cast<int>(id) && knob->isEditing();
    active_gesture |= section.delay && section.delay->getTag()==static_cast<int>(id) && section.delay->isEditing();
    active_gesture |= section.link && section.link->getTag()==static_cast<int>(id) && section.link->isEditing();
    for (auto* mute : section.mute)
      active_gesture |= mute && mute->getTag()==static_cast<int>(id) && mute->isEditing();
  }
  if (!active_gesture) controller->beginEdit(id);
  if (controller->setParamNormalized(id, normalized) == Steinberg::kResultTrue) {
    controller->performEdit(id, controller->getParamNormalized(id));
    for(const bool input : {true,false}) {
      const auto left=input ? kInputGain : kOutputGain;
      const auto right=input ? kInputGainRight : kOutputGainRight;
      const auto link=input ? kInputLink : kOutputLink;
      if(controller->getParamNormalized(link)>0.5 && (id==left || id==right || id==link)) {
        const auto peer=id==right ? left : right;
        controller->beginEdit(peer); controller->performEdit(peer,controller->getParamNormalized(peer)); controller->endEdit(peer);
      }
    }
    controller->SyncParameterStateToProcessor();
  }
  if (!active_gesture) controller->endEdit(id);
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
                                       ? "●  UNAVAILABLE"
                                       : (host_route_connected
                                              ? "●  DAW / HOST"
                                              : "●  OFF")));
  status_label_->setText(text);
  status_label_->setFontColor(
      status == audio::Status::kUnavailable
          ? colors_.kDanger
          : (status == audio::Status::kActive ? colors_.kAccent
                                               : colors_.kMuted));
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
  if (!control) return;
  if (control == theme_button_) {
    if (control->getValue() > 0.5F) SetLightTheme(!colors_.light);
    return;
  }

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
  for(int section=0;section<2;++section) {
    auto& c=sections_[section]; const bool input=section==0;
    if(control==c.reset) {
      const auto link = input ? kInputLink : kOutputLink;
      const auto mute_left = input ? kInputMuteLeft : kOutputMuteLeft;
      const auto mute_right = input ? kInputMuteRight : kOutputMuteRight;
      SendParameterEdit(link, 1.0);
      SendParameterEdit(input ? kInputGain : kOutputGain, GainDbToNormalized(0));
      SendParameterEdit(input ? kInputGainRight : kOutputGainRight, GainDbToNormalized(0));
      SendParameterEdit(mute_left, 0.0);
      SendParameterEdit(mute_right, 0.0);
      RefreshParameters(); return;
    }
    if(control==c.delay_reset) {
      SendParameterEdit(input ? kInputDelay : kOutputDelay,0); RefreshParameters(); return;
    }
    if(control==c.link || control==c.mute[0] || control==c.mute[1]) {
      SendParameterEdit(control->getTag(),control->getValueNormalized()); RefreshParameters(); return;
    }
    for(int ch=0;ch<2;++ch) {
      if(control==c.gain[ch] || control==c.gain_edit[ch]) {
        double value=c.gain[ch]->getValue();
        if(control==c.gain_edit[ch]) ui::ParseValue(c.gain_edit[ch]->getText().getString(),"dB",kMinimumGainDb,kMaximumGainDb,value);
        SendParameterEdit(c.gain[ch]->getTag(),GainDbToNormalized(value));
        SetGainText(c.gain_edit[ch],value); RefreshParameters(); return;
      }
    }
    if(control==c.delay || control==c.delay_edit) {
      double value=c.delay->getValue();
      if(control==c.delay_edit) {
        auto* controller=static_cast<Controller*>(getController());
        value=DelayMsFromNormalized(controller->getParamNormalized(c.delay->getTag()));
        ui::ParseValue(c.delay_edit->getText().getString(),"ms",0,kMaximumDelayMs,value);
      }
      SendParameterEdit(c.delay->getTag(),DelayMsToNormalized(value));
      SetDelayText(c.delay_edit,value); RefreshParameters(); return;
    }
  }
}

}  // namespace audio_bridge::vst
