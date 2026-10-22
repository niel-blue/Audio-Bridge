#ifndef AUDIO_BRIDGE_EDITOR_WIDGETS_H_
#define AUDIO_BRIDGE_EDITOR_WIDGETS_H_

#include <string>
#include "ui/editor_palette.h"
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


class LinkSwitch final : public VSTGUI::CTextButton {
 public:
  LinkSwitch(const VSTGUI::CRect& rect, VSTGUI::IControlListener* listener, int tag, const Palette& colors = kDarkPalette);
  void draw(VSTGUI::CDrawContext* context) override;
 private:
  const Palette& colors_;
};

class RotaryKnob final : public VSTGUI::CKnob {
 public:
  RotaryKnob(const VSTGUI::CRect& rect, VSTGUI::IControlListener* listener,
             int tag, bool delay, const Palette& colors = kDarkPalette);
  void draw(VSTGUI::CDrawContext* context) override;
  VSTGUI::CMouseEventResult onMouseDown(VSTGUI::CPoint& where,
      const VSTGUI::CButtonState& buttons) override;
  VSTGUI::CMouseEventResult onMouseMoved(VSTGUI::CPoint& where,
      const VSTGUI::CButtonState& buttons) override;

 private:
  const Palette& colors_;
  VSTGUI::CCoord drag_start_x_ = 0;

};

class LevelMeter final : public VSTGUI::CView {
 public:
  explicit LevelMeter(const VSTGUI::CRect& rect, const Palette& colors = kDarkPalette);

  void SetPeak(float peak);
  void Clear();
  void draw(VSTGUI::CDrawContext* context) override;

 private:
  const Palette& colors_;
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
                   VSTGUI::CFontRef menu_font, const Palette& colors = kDarkPalette) -> VSTGUI::COptionMenu*;
auto AddLevelMeter(VSTGUI::CViewContainer* panel, double top, const Palette& colors = kDarkPalette) -> LevelMeter*;
auto AddParameterControl(VSTGUI::CViewContainer* panel,
                         VSTGUI::IControlListener* listener,
                         VSTGUI::CFontRef font, int tag, double top,
                         float minimum, float maximum, float default_value,
                         float wheel_increment, const char* default_text, const Palette& colors = kDarkPalette)
    -> ParameterControl;

auto FormatDb(double value) -> std::string;
auto FormatMilliseconds(double value) -> std::string;
auto ParseValue(const std::string& text, const std::string& suffix,
                double minimum, double maximum, double& value) -> bool;

}  // namespace audio_bridge::vst::ui

#endif
