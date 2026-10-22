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
  int32_t getKnobMode() const override;
  void SetLightTheme(bool light, bool notify_host = true);
  bool IsLightTheme() const { return colors_.light; }
  static constexpr int kThemeControlTag = 10000;

 private:
  void BuildFrame(void* parent);
  void RefreshDevices();
  void RefreshParameters();
  void SendDeviceConfig();
  void SendParameterEdit(ParamID id, double normalized);
  void PollState();
  void UpdateStatus();
  void SetGainText(VSTGUI::CTextEdit* edit, double value);
  void SetDelayText(VSTGUI::CTextEdit* edit, double value);

  VSTGUI::CFontRef font_ = nullptr;
  ui::Palette colors_ = ui::Palette::From(false);
  VSTGUI::CTextButton* theme_button_ = nullptr;
  VSTGUI::CFontRef small_font_ = nullptr;
  VSTGUI::CFontRef heading_font_ = nullptr;
  VSTGUI::CFontRef strong_font_ = nullptr;
  VSTGUI::COptionMenu* input_menu_ = nullptr;
  VSTGUI::COptionMenu* output_menu_ = nullptr;
  VSTGUI::CTextButton* input_refresh_ = nullptr;
  VSTGUI::CCheckBox* exclusive_checkbox_ = nullptr;
  struct SectionControls {
    ui::RotaryKnob* gain[2]{};
    VSTGUI::CTextEdit* gain_edit[2]{};
    VSTGUI::CTextButton* mute[2]{};
    VSTGUI::CTextButton* link = nullptr;
    VSTGUI::CTextButton* reset = nullptr;
    ui::RotaryKnob* delay = nullptr;
    VSTGUI::CTextEdit* delay_edit = nullptr;
    VSTGUI::CTextButton* delay_reset = nullptr;
  } sections_[2];
  VSTGUI::CTextLabel* status_label_ = nullptr;
  ui::LevelMeter* input_meter_ = nullptr;
  ui::LevelMeter* output_meter_ = nullptr;
  std::vector<std::string> input_ids_;
  std::vector<std::string> output_ids_;
  VSTGUI::SharedPointer<VSTGUI::CVSTGUITimer> timer_;
};

}  // namespace audio_bridge::vst

#endif
