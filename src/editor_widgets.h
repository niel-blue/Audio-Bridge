#ifndef AUDIO_BRIDGE_EDITOR_WIDGETS_H_
#define AUDIO_BRIDGE_EDITOR_WIDGETS_H_

#include <string>

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
inline constexpr auto kBackground = VSTGUI::CColor(0x0c, 0x18, 0x2b);
inline constexpr auto kPanelRaised = VSTGUI::CColor(0x12, 0x31, 0x4f);
inline constexpr auto kControlBackground = VSTGUI::CColor(0x07, 0x14, 0x24);
inline constexpr auto kBorder = VSTGUI::CColor(0x1d, 0x45, 0x65);
inline constexpr auto kAccent = VSTGUI::CColor(0x4f, 0xc7, 0xff);
inline constexpr auto kAccentDim = VSTGUI::CColor(0x12, 0x7b, 0xa1);
inline constexpr auto kReset = VSTGUI::CColor(0x17, 0xa5, 0xd5);
inline constexpr auto kResetPressed = VSTGUI::CColor(0x4f, 0xc7, 0xff);
inline constexpr auto kText = VSTGUI::CColor(0xe6, 0xf4, 0xfd);
inline constexpr auto kMuted = VSTGUI::CColor(0x74, 0x8e, 0xa4);
inline constexpr auto kDanger = VSTGUI::CColor(0xff, 0x74, 0x74);
}  // namespace palette

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
