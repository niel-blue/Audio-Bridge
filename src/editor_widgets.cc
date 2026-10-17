#include "editor_widgets.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <sstream>

#include "vstgui4/vstgui/lib/cbitmap.h"
#include "vstgui4/vstgui/lib/cdrawcontext.h"
#include "vstgui4/vstgui/lib/cgradient.h"

namespace audio_bridge::vst::ui {
namespace {

class SolidBitmap final : public VSTGUI::CBitmap {
 public:
  SolidBitmap(const int width, const int height, const VSTGUI::CColor fill,
              const VSTGUI::CColor frame)
      : CBitmap(width, height), fill_(fill), frame_(frame) {}

  void draw(VSTGUI::CDrawContext* const context, const VSTGUI::CRect& rect,
            const VSTGUI::CPoint& offset = VSTGUI::CPoint(0, 0),
            const float /*alpha*/ = 1.0F) override {
    auto target = rect;
    target.offset(offset);
    context->setFillColor(fill_);
    context->drawRect(target, VSTGUI::kDrawFilled);
    if (frame_ != VSTGUI::kTransparentCColor) {
      context->setFrameColor(frame_);
      context->drawRect(target, VSTGUI::kDrawStroked);
    }
  }

 private:
  VSTGUI::CColor fill_;
  VSTGUI::CColor frame_;
};

class SliderTrackBitmap final : public VSTGUI::CBitmap {
 public:
  SliderTrackBitmap(const int width, const int height,
                    const VSTGUI::CColor fill, const VSTGUI::CColor frame)
      : CBitmap(width, height), fill_(fill), frame_(frame) {}

  void draw(VSTGUI::CDrawContext* const context, const VSTGUI::CRect& rect,
            const VSTGUI::CPoint& offset = VSTGUI::CPoint(0, 0),
            const float /*alpha*/ = 1.0F) override {
    auto target = rect;
    target.offset(offset);
    constexpr auto kTrackHeight = 10.0;
    target.top += (target.getHeight() - kTrackHeight) * 0.5;
    target.bottom = target.top + kTrackHeight;
    context->setFillColor(fill_);
    context->drawRect(target, VSTGUI::kDrawFilled);
    context->setFrameColor(frame_);
    context->drawRect(target, VSTGUI::kDrawStroked);
  }

 private:
  VSTGUI::CColor fill_;
  VSTGUI::CColor frame_;
};

class DeviceMenu final : public VSTGUI::COptionMenu {
 public:
  DeviceMenu(const VSTGUI::CRect& rect,
             VSTGUI::IControlListener* const listener, const int tag,
             VSTGUI::CBitmap* const background)
      : COptionMenu(rect, listener, tag, background) {}

  void draw(VSTGUI::CDrawContext* const context) override {
    COptionMenu::draw(context);
    const auto rect = getViewSize();
    auto arrow = rect;
    arrow.left = arrow.right - 24.0;
    context->setFillColor(palette::kControlBackground);
    context->drawRect(arrow, VSTGUI::kDrawFilled);
    context->setFrameColor(palette::kBorder);
    context->drawLine(VSTGUI::CPoint(arrow.left, arrow.top),
                      VSTGUI::CPoint(arrow.left, arrow.bottom));

    const auto center_x = arrow.left + arrow.getWidth() * 0.5;
    const auto center_y = arrow.top + arrow.getHeight() * 0.5;
    context->setFrameColor(palette::kAccent);
    context->setLineWidth(1.5);
    context->drawLine(VSTGUI::CPoint(center_x - 4.0, center_y - 2.0),
                      VSTGUI::CPoint(center_x, center_y + 2.0));
    context->drawLine(VSTGUI::CPoint(center_x, center_y + 2.0),
                      VSTGUI::CPoint(center_x + 4.0, center_y - 2.0));
    context->setFrameColor(palette::kBorder);
    context->drawRect(rect, VSTGUI::kDrawStroked);
    setDirty(false);
  }
};

}  // namespace

LevelMeter::LevelMeter(const VSTGUI::CRect& rect) : CView(rect) {
  setMouseEnabled(false);
}

void LevelMeter::SetPeak(const float peak) {
  const auto next = std::max(std::clamp(peak, 0.0F, 2.0F), level_ * 0.86F);
  if (std::abs(next - level_) > 0.0001F) {
    level_ = next;
    invalid();
  }
}

void LevelMeter::Clear() {
  if (level_ != 0.0F) {
    level_ = 0.0F;
    invalid();
  }
}

void LevelMeter::draw(VSTGUI::CDrawContext* const context) {
  auto rect = getViewSize();
  context->setFillColor(palette::kControlBackground);
  context->drawRect(rect, VSTGUI::kDrawFilled);
  rect.inset(1.0, 1.0);
  const auto db = level_ > 0.000001F ? 20.0F * std::log10(level_) : -60.0F;
  const auto normalized = std::clamp((db + 60.0F) / 60.0F, 0.0F, 1.0F);
  if (normalized > 0.0F) {
    auto active = rect;
    active.right = active.left + active.getWidth() * normalized;
    context->setFillColor(level_ >= 1.0F ? palette::kDanger
                                         : palette::kAccent);
    context->drawRect(active, VSTGUI::kDrawFilled);
  }
  context->setFrameColor(palette::kBorder);
  context->drawRect(rect, VSTGUI::kDrawStroked);
  setDirty(false);
}

auto MakeLabel(const VSTGUI::CRect& rect, const char* const text,
               VSTGUI::CFontRef font, const VSTGUI::CColor color)
    -> VSTGUI::CTextLabel* {
  auto* const label = new VSTGUI::CTextLabel(rect, text, nullptr,
                                             VSTGUI::CParamDisplay::kNoFrame);
  label->setFont(font);
  label->setFontColor(color);
  label->setBackColor(VSTGUI::kTransparentCColor);
  label->setTransparency(true);
  label->setHoriAlign(VSTGUI::CHoriTxtAlign::kLeftText);
  return label;
}

auto AddDeviceMenu(VSTGUI::CViewContainer* const panel,
                   VSTGUI::IControlListener* const listener,
                   const char* const title, VSTGUI::CFontRef heading_font,
                   VSTGUI::CFontRef menu_font) -> VSTGUI::COptionMenu* {
  auto* const title_label = MakeLabel(VSTGUI::CRect(0, 5, 320, 19), title,
                                      heading_font, palette::kAccent);
  title_label->setAutosizeFlags(VSTGUI::kAutosizeLeft | VSTGUI::kAutosizeRight |
                                VSTGUI::kAutosizeTop);
  panel->addView(title_label);
  auto* const background = new SolidBitmap(320, 26, palette::kPanelRaised,
                                            palette::kBorder);
  auto* const menu = new DeviceMenu(VSTGUI::CRect(0, 22, 320, 48), listener,
                                    -1, background);
  menu->setAutosizeFlags(VSTGUI::kAutosizeLeft | VSTGUI::kAutosizeRight |
                         VSTGUI::kAutosizeTop);
  menu->setFont(menu_font);
  menu->setFontColor(palette::kText);
  menu->setHoriAlign(VSTGUI::CHoriTxtAlign::kLeftText);
  menu->setTextInset(VSTGUI::CPoint(8, 0));
  menu->addEntry("OFF");
  panel->addView(menu);
  background->forget();
  return menu;
}

auto AddLevelMeter(VSTGUI::CViewContainer* const panel,
                   const double top) -> LevelMeter* {
  auto* const meter =
      new LevelMeter(VSTGUI::CRect(0, top, 320, top + 6));
  meter->setAutosizeFlags(VSTGUI::kAutosizeLeft | VSTGUI::kAutosizeRight |
                          VSTGUI::kAutosizeTop);
  panel->addView(meter);
  return meter;
}

auto AddParameterControl(
    VSTGUI::CViewContainer* const panel,
    VSTGUI::IControlListener* const listener, VSTGUI::CFontRef font,
    const int tag, const double top, const float minimum, const float maximum,
    const float default_value, const float wheel_increment,
    const char* const default_text) -> ParameterControl {
  auto result = ParameterControl{};
  auto* const edit_background =
      new SolidBitmap(50, 20, palette::kPanelRaised, palette::kBorder);
  result.edit = new VSTGUI::CTextEdit(
      VSTGUI::CRect(0, top, 50, top + 20), listener, -1, default_text,
      edit_background);
  result.edit->setFont(font);
  result.edit->setFontColor(palette::kText);
  result.edit->setHoriAlign(VSTGUI::CHoriTxtAlign::kRightText);
  result.edit->setTextInset(VSTGUI::CPoint(4, 0));
  result.edit->setImmediateTextChange(false);
  result.edit->setAutosizeFlags(VSTGUI::kAutosizeLeft | VSTGUI::kAutosizeTop);
  panel->addView(result.edit);
  edit_background->forget();

  auto* const background = new SliderTrackBitmap(
      210, 18, palette::kControlBackground, palette::kBorder);
  auto* const handle =
      new SolidBitmap(8, 18, palette::kAccent, palette::kAccent);
  result.slider = new VSTGUI::CHorizontalSlider(
      VSTGUI::CRect(60, top + 1, 270, top + 19), listener, tag, 60, 262,
      handle, background, VSTGUI::CPoint(0, 0),
      VSTGUI::CSliderBase::kLeft);
  result.slider->setAutosizeFlags(VSTGUI::kAutosizeLeft | VSTGUI::kAutosizeRight |
                             VSTGUI::kAutosizeTop);
  result.slider->setMin(minimum);
  result.slider->setMax(maximum);
  result.slider->setDefaultValue(default_value);
  result.slider->setValue(default_value);
  result.slider->setWheelInc(wheel_increment);
  panel->addView(result.slider);
  background->forget();
  handle->forget();

  result.reset = new VSTGUI::CTextButton(
      VSTGUI::CRect(270, top, 320, top + 20), listener, -1, "RESET");
  result.reset->setFont(font);
  result.reset->setTextColor(palette::kText);
  result.reset->setTextColorHighlighted(palette::kText);
  result.reset->setFrameColor(palette::kReset);
  result.reset->setFrameColorHighlighted(palette::kResetPressed);
  auto* const reset_gradient = VSTGUI::CGradient::create(
      0, 1, palette::kReset, palette::kAccentDim);
  auto* const reset_gradient_pressed = VSTGUI::CGradient::create(
      0, 1, palette::kResetPressed, palette::kReset);
  result.reset->setGradient(reset_gradient);
  result.reset->setGradientHighlighted(reset_gradient_pressed);
  reset_gradient->forget();
  reset_gradient_pressed->forget();
  result.reset->setRoundRadius(3.0);
  result.reset->setFrameWidth(1.0);
  result.reset->setTextMargin(1.0);
  result.reset->setTextAlignment(VSTGUI::kCenterText);
  result.reset->setAutosizeFlags(VSTGUI::kAutosizeRight | VSTGUI::kAutosizeTop);
  panel->addView(result.reset);
  return result;
}

auto FormatDb(const double value) -> std::string {
  auto stream = std::ostringstream{};
  stream << std::fixed << std::setprecision(1) << value << " dB";
  return stream.str();
}

auto FormatMilliseconds(const double value) -> std::string {
  auto stream = std::ostringstream{};
  stream << std::fixed << std::setprecision(value >= 1000.0 ? 0 : 1) << value
         << " ms";
  return stream.str();
}

auto ParseValue(const std::string& text, const std::string& suffix,
                const double minimum, const double maximum,
                double& value) -> bool {
  std::size_t parsed = 0;
  double candidate = 0.0;
  try {
    candidate = std::stod(text, &parsed);
  } catch (...) {
    return false;
  }
  while (parsed < text.size() &&
         std::isspace(static_cast<unsigned char>(text[parsed])) != 0) {
    ++parsed;
  }
  if (parsed < text.size()) {
    if (text.compare(parsed, suffix.size(), suffix) != 0) {
      return false;
    }
    parsed += suffix.size();
  }
  while (parsed < text.size() &&
         std::isspace(static_cast<unsigned char>(text[parsed])) != 0) {
    ++parsed;
  }
  if (parsed != text.size() || !std::isfinite(candidate)) {
    return false;
  }
  value = std::clamp(candidate, minimum, maximum);
  return true;
}

}  // namespace audio_bridge::vst::ui
