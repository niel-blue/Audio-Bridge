#ifndef AUDIO_BRIDGE_EDITOR_H_
#define AUDIO_BRIDGE_EDITOR_H

#include <string>
#include <vector>

#include "pluginterfaces/base/fplatform.h"
#include "pluginterfaces/vst/vsttypes.h"
#include "public.sdk/source/vst/vstguieditor.h"
#include "vstgui4/vstgui/lib/cview.h"
#include "vstgui4/vstgui/lib/controls/ccontrol.h"
#include "vstgui4/vstgui/lib/controls/cbuttons.h"
#include "vstgui4/vstgui/lib/controls/ctextedit.h"
#include "vstgui4/vstgui/lib/cvstguitimer.h"
#include "vstgui4/vstgui/lib/vstguifwd.h"

#include "editor_widgets.h"

namespace audio_bridge::vst {

class Controller;

class Editor final : public Steinberg::Vst::VSTGUIEditor,
                     public VSTGUI::IControlListener {
  using PlatformType = VSTGUI::PlatformType;
  using ParamID = Steinberg::Vst::ParamID;
  using CControl = VSTGUI::CControl;

 public:
  explicit Editor(void* controller);
  ~Editor() SMTG_OVERRIDE;

  auto PLUGIN_API open(void* parent, const PlatformType& platformType)
      -> bool SMTG_OVERRIDE;
  void PLUGIN_API close() SMTG_OVERRIDE;
  void valueChanged(CControl* control) SMTG_OVERRIDE;

 private:
  void BuildFrame(void* parent);
  void RefreshDevices();
  void RefreshParameters();
  void UpdateHostRouteLabels(bool input_connected, bool output_connected);
  void SendDeviceConfig();
  void SendParameterEdit(ParamID id, double normalized);
  void PollState();
  void UpdateStatus();
  void SetGainText(VSTGUI::CTextEdit* edit, double value);
  void SetDelayText(VSTGUI::CTextEdit* edit, double value);

  VSTGUI::CFontRef font_ = nullptr;
  VSTGUI::CFontRef small_font_ = nullptr;
  VSTGUI::CFontRef heading_font_ = nullptr;
  VSTGUI::CFontRef strong_font_ = nullptr;
  VSTGUI::COptionMenu* input_menu_ = nullptr;
  VSTGUI::COptionMenu* output_menu_ = nullptr;
  VSTGUI::CCheckBox* exclusive_checkbox_ = nullptr;
  VSTGUI::CHorizontalSlider* input_gain_ = nullptr;
  VSTGUI::CHorizontalSlider* output_gain_ = nullptr;
  VSTGUI::CHorizontalSlider* output_delay_ = nullptr;
  VSTGUI::CTextButton* input_reset_ = nullptr;
  VSTGUI::CTextButton* output_reset_ = nullptr;
  VSTGUI::CTextButton* output_delay_reset_ = nullptr;
  VSTGUI::CTextEdit* input_gain_edit_ = nullptr;
  VSTGUI::CTextEdit* output_gain_edit_ = nullptr;
  VSTGUI::CTextEdit* output_delay_edit_ = nullptr;
  VSTGUI::CTextLabel* status_label_ = nullptr;
  ui::LevelMeter* input_meter_ = nullptr;
  ui::LevelMeter* output_meter_ = nullptr;
  std::vector<std::string> input_ids_;
  std::vector<std::string> output_ids_;
  VSTGUI::SharedPointer<VSTGUI::CVSTGUITimer> timer_;
};

}  // namespace audio_bridge::vst

#endif
