#ifndef AUDIO_BRIDGE_EDITOR_WIDGETS_H_
#define AUDIO_BRIDGE_EDITOR_WIDGETS_H_

#include <string>
#include "vstgui4/vstgui/lib/controls/cknob.h"

#include "vstgui4/vstgui/lib/ccolor.h"
#include "vstgui4/vstgui/lib/cfont.h"
#include "vstgui4/vstgui/lib/cview.h"
#include "vstgui4/vstgui/lib/cviewcontainer.h"
#include "vstgui4/vstgui/lib/controls/cbuttons.h"
#include "vstgui4/vstgui/lib/controls/coptionmenu.h"
#include "vstgui4/vstgui/lib/controls/cslider.h"
#include "vstgui4/vstgui/lib/controls/ctextedit.h"
#include "vstgui4/vstgui/lib/controls/ctextlabel.h"

namespace audio_bridge::vst::ui {

namespace palette {
inline constexpr auto kBackground = VSTGUI::CColor(0x0c, 0x16, 0x23);
inline constexpr auto kPanelRaised = VSTGUI::CColor(0x14, 0x38, 0x56);
inline constexpr auto kControlBackground = VSTGUI::CColor(0x24, 0x44, 0x5d);
inline constexpr auto kSliderTrack = VSTGUI::CColor(0x1c, 0x4c, 0x6c);
inline constexpr auto kBorder = VSTGUI::CColor(0x27, 0x49, 0x61);
inline constexpr auto kAccent = VSTGUI::CColor(0x06, 0xc9, 0xff);
inline constexpr auto kButtonBorder = VSTGUI::CColor(0x45, 0x6f, 0x89);
inline constexpr auto kReset = VSTGUI::CColor(0x00, 0xb2, 0xeb);
inline constexpr auto kResetPressed = VSTGUI::CColor(0x14, 0x38, 0x56);
inline constexpr auto kText = VSTGUI::CColor(0xe7, 0xf4, 0xfb);
inline constexpr auto kMuted = VSTGUI::CColor(0x77, 0x94, 0xa8);
inline constexpr auto kDanger = VSTGUI::CColor(0xff, 0x74, 0x74);
}  // namespace palette

class LinkSwitch final : public VSTGUI::CTextButton {
 public:
  LinkSwitch(const VSTGUI::CRect& rect, VSTGUI::IControlListener* listener, int tag);
  void draw(VSTGUI::CDrawContext* context) override;
};

class RotaryKnob final : public VSTGUI::CKnob {
 public:
  RotaryKnob(const VSTGUI::CRect& rect, VSTGUI::IControlListener* listener,
             int tag, bool delay);
  void draw(VSTGUI::CDrawContext* context) override;
  VSTGUI::CMouseEventResult onMouseDown(VSTGUI::CPoint& where,
      const VSTGUI::CButtonState& buttons) override;
  VSTGUI::CMouseEventResult onMouseMoved(VSTGUI::CPoint& where,
      const VSTGUI::CButtonState& buttons) override;

 private:
  VSTGUI::CCoord drag_start_x_ = 0;

};

class LevelMeter final : public VSTGUI::CView {
 public:
  explicit LevelMeter(const VSTGUI::CRect& rect);

  void SetPeak(float peak);
  void Clear();
  void draw(VSTGUI::CDrawContext* context) override;

 private:
  float level_ = 0.0F;
};

struct ParameterControl {
  VSTGUI::CHorizontalSlider* slider = nullptr;
  VSTGUI::CTextButton* reset = nullptr;
  VSTGUI::CTextEdit* edit = nullptr;
};

auto MakeLabel(const VSTGUI::CRect& rect, const char* text,
               VSTGUI::CFontRef font, VSTGUI::CColor color)
    -> VSTGUI::CTextLabel*;
auto AddDeviceMenu(VSTGUI::CViewContainer* panel,
                   VSTGUI::IControlListener* listener, const char* title,
                   VSTGUI::CFontRef heading_font,
                   VSTGUI::CFontRef menu_font) -> VSTGUI::COptionMenu*;
auto AddLevelMeter(VSTGUI::CViewContainer* panel, double top) -> LevelMeter*;
auto AddParameterControl(VSTGUI::CViewContainer* panel,
                         VSTGUI::IControlListener* listener,
                         VSTGUI::CFontRef font, int tag, double top,
                         float minimum, float maximum, float default_value,
                         float wheel_increment, const char* default_text)
    -> ParameterControl;

auto FormatDb(double value) -> std::string;
auto FormatMilliseconds(double value) -> std::string;
auto ParseValue(const std::string& text, const std::string& suffix,
                double minimum, double maximum, double& value) -> bool;

}  // namespace audio_bridge::vst::ui

#endif
