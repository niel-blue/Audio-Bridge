#include "editor_widgets.h"
#include "parameters.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <sstream>

#include "vstgui4/vstgui/lib/cbitmap.h"
#include "vstgui4/vstgui/lib/cdrawcontext.h"
#include "vstgui4/vstgui/lib/cgradient.h"
#include "vstgui4/vstgui/lib/cgraphicspath.h"

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
                    const VSTGUI::CColor color)
      : CBitmap(width, height), color_(color) {}

  void draw(VSTGUI::CDrawContext* const context, const VSTGUI::CRect& rect,
            const VSTGUI::CPoint& offset = VSTGUI::CPoint(0, 0),
            const float /*alpha*/ = 1.0F) override {
    auto target = rect;
    target.offset(offset);
    const auto center_y = target.top + target.getHeight() * 0.5;
    context->setDrawMode(VSTGUI::kAntiAliasing);
    context->setLineStyle(
        VSTGUI::CLineStyle(VSTGUI::CLineStyle::kLineCapRound));
    context->setLineWidth(2.0);
    context->setFrameColor(color_);
    context->drawLine(
        VSTGUI::CPoint(target.left + 2.5, center_y),
        VSTGUI::CPoint(target.right - 2.5, center_y));
  }

 private:
  VSTGUI::CColor color_;
};

class SliderHandleBitmap final : public VSTGUI::CBitmap {
 public:
  explicit SliderHandleBitmap(const VSTGUI::CColor color)
      : CBitmap(5, 5), color_(color) {}

  void draw(VSTGUI::CDrawContext* const context, const VSTGUI::CRect& rect,
            const VSTGUI::CPoint& offset = VSTGUI::CPoint(0, 0),
            const float /*alpha*/ = 1.0F) override {
    auto target = rect;
    target.offset(offset);
    context->setDrawMode(VSTGUI::kAntiAliasing);
    context->setFillColor(color_);
    context->drawEllipse(target, VSTGUI::kDrawFilled);
  }

 private:
  VSTGUI::CColor color_;
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
    context->setFillColor(palette::kBackground);
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

LinkSwitch::LinkSwitch(const VSTGUI::CRect& rect,VSTGUI::IControlListener* listener,int tag)
    : CTextButton(rect,listener,tag,"LINK ON",VSTGUI::CTextButton::kOnOffStyle) {}

void LinkSwitch::draw(VSTGUI::CDrawContext* context) {
  const auto r=getViewSize(); const double cx=(r.left+r.right)/2;
  const bool on=getValue()>0.5F;
  context->setDrawMode(VSTGUI::kAntiAliasing);
  context->setFont(VSTGUI::kNormalFontVerySmall);
  context->setFontColor(on ? palette::kAccent : palette::kMuted);
  context->drawString(on ? "LINK ON" : "LINK OFF",VSTGUI::CRect(r.left,r.top,r.right,r.top+11),VSTGUI::kCenterText);
  auto capsule=VSTGUI::owned(context->createGraphicsPath());
  if(capsule) {
    capsule->addRoundRect(VSTGUI::CRect(cx-10,r.top+13,cx+10,r.top+23),5);
    context->setFillColor(on ? palette::kPanelRaised : palette::kBackground);
    context->setFrameColor(on ? palette::kAccent : palette::kMuted); context->setLineWidth(1);
    context->drawGraphicsPath(capsule,VSTGUI::CDrawContext::kPathFilled);
    context->drawGraphicsPath(capsule,VSTGUI::CDrawContext::kPathStroked);
  }
  context->setFillColor(on ? palette::kText : palette::kMuted);
  const double dot=cx+(on ? 5 : -5);
  context->drawEllipse(VSTGUI::CRect(dot-3,r.top+15,dot+3,r.top+21),VSTGUI::kDrawFilled);
  context->setFrameColor(on ? palette::kAccent : palette::kMuted);context->setLineWidth(1.3);
  context->drawEllipse(VSTGUI::CRect(cx-6,r.top+28,cx+1,r.top+34),VSTGUI::kDrawStroked);
  context->drawEllipse(VSTGUI::CRect(cx-1,r.top+25,cx+6,r.top+31),VSTGUI::kDrawStroked);
  setDirty(false);
}

RotaryKnob::RotaryKnob(const VSTGUI::CRect& rect,
    VSTGUI::IControlListener* listener, int tag, bool delay)
    : CKnob(rect, listener, tag, nullptr, nullptr) {
  setMin(delay ? 0.0F : -20.0F);
  setMax(delay ? static_cast<float>(kMaximumKnobDelayMs) : 20.0F);
  setDefaultValue(0.0F);
  setValue(0.0F);
  setWheelInc(delay ? 10.0F / static_cast<float>(kMaximumKnobDelayMs) : 0.0125F);
  setKnobRange(delay ? 200.0F : 160.0F);
}

VSTGUI::CMouseEventResult RotaryKnob::onMouseDown(
    VSTGUI::CPoint& where, const VSTGUI::CButtonState& buttons) {
  drag_start_x_ = where.x;
  // Keep the standard relative drag, fine adjustment and edit notifications.
  // Alt must not switch this control back to circular tracking.
  return CKnob::onMouseDown(where, VSTGUI::CButtonState(buttons() & ~VSTGUI::kAlt));
}

VSTGUI::CMouseEventResult RotaryKnob::onMouseMoved(
    VSTGUI::CPoint& where, const VSTGUI::CButtonState& buttons) {
  // VSTGUI's linear mode normally adds horizontal movement to vertical movement.
  // Project onto the original X position so only upward/downward motion counts.
  auto vertical = where;
  vertical.x = drag_start_x_;
  return CKnob::onMouseMoved(vertical, VSTGUI::CButtonState(buttons() & ~VSTGUI::kAlt));
}

void RotaryKnob::draw(VSTGUI::CDrawContext* context) {
  const auto r = getViewSize();
  const double cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2;
  const double radius = r.getWidth() / 2 - 3;
  constexpr double pi = 3.141592653589793;
  context->setDrawMode(VSTGUI::kAntiAliasing);
  context->setLineStyle(VSTGUI::CLineStyle(VSTGUI::CLineStyle::kLineCapRound));
  const auto point = [&](double a, double rad) {
    return VSTGUI::CPoint(cx + std::cos(a) * rad, cy + std::sin(a) * rad);
  };
  const auto arc = [&](double rad, double fraction, VSTGUI::CColor color, double width) {
    context->setFrameColor(color); context->setLineWidth(width);
    if (fraction <= 0) return;
    auto path = VSTGUI::owned(context->createGraphicsPath());
    if (!path) return;
    CKnob::addArc(path, VSTGUI::CRect(cx-rad,cy-rad,cx+rad,cy+rad),0.75*pi,1.5*pi*fraction);
    context->drawGraphicsPath(path,VSTGUI::CDrawContext::kPathStroked);
  };
  arc(radius, 1, palette::kSliderTrack, 3.5);
  arc(radius, getValueNormalized(), palette::kAccent, 2.5);
  auto body = r; body.inset(8,8);
  context->setFillColor(palette::kPanelRaised);
  context->setFrameColor(palette::kButtonBorder); context->setLineWidth(1);
  auto body_path = VSTGUI::owned(context->createGraphicsPath());
  if (body_path) {
    body_path->addEllipse(body);
    auto gradient=VSTGUI::owned(VSTGUI::CGradient::create(0,1,VSTGUI::CColor(30,55,72),palette::kBackground));
    context->fillLinearGradient(body_path,*gradient,VSTGUI::CPoint(cx,body.top),VSTGUI::CPoint(cx,body.bottom));
    context->drawGraphicsPath(body_path,VSTGUI::CDrawContext::kPathStroked);
  }
  const double angle = 0.75*pi + getValueNormalized()*1.5*pi;
  context->setFrameColor(palette::kText); context->setLineWidth(1.5);
  context->drawLine(point(angle, radius*0.32), point(angle, radius-2));
  setDirty(false);
}

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
  auto* const title_label = MakeLabel(VSTGUI::CRect(10, 5, 290, 19), title,
                                      heading_font, palette::kAccent);
  panel->addView(title_label);
  auto* const background = new SolidBitmap(280, 26, palette::kPanelRaised,
                                            palette::kBorder);
  auto* const menu = new DeviceMenu(VSTGUI::CRect(10, 22, 290, 48), listener,
                                    -1, background);
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
      new LevelMeter(VSTGUI::CRect(10, top, 290, top + 6));
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
      VSTGUI::CRect(10, top, 60, top + 20), listener, -1, default_text,
      edit_background);
  result.edit->setFont(font);
  result.edit->setFontColor(palette::kText);
  result.edit->setHoriAlign(VSTGUI::CHoriTxtAlign::kRightText);
  result.edit->setTextInset(VSTGUI::CPoint(4, 0));
  result.edit->setImmediateTextChange(false);
  panel->addView(result.edit);
  edit_background->forget();

  auto* const background =
      new SliderTrackBitmap(168, 18, palette::kSliderTrack);
  auto* const handle = new SliderHandleBitmap(palette::kAccent);
  result.slider = new VSTGUI::CHorizontalSlider(
      VSTGUI::CRect(70, top + 1, 238, top + 19), listener, tag, 70, 233,
      handle, background, VSTGUI::CPoint(0, 0),
      VSTGUI::CSliderBase::kLeft);
  result.slider->setOffsetHandle(VSTGUI::CPoint(0, 6.5));
  result.slider->setMin(minimum);
  result.slider->setMax(maximum);
  result.slider->setDefaultValue(default_value);
  result.slider->setValue(default_value);
  result.slider->setWheelInc(wheel_increment);
  panel->addView(result.slider);
  background->forget();
  handle->forget();

  result.reset = new VSTGUI::CTextButton(
      VSTGUI::CRect(246, top, 290, top + 20), listener, -1, "RESET");
  result.reset->setFont(font);
  result.reset->setTextColor(palette::kText);
  result.reset->setTextColorHighlighted(palette::kText);
  result.reset->setFrameColor(palette::kAccent);
  result.reset->setFrameColorHighlighted(palette::kAccent);
  auto* const reset_gradient = VSTGUI::CGradient::create(
      0, 1, palette::kResetPressed, palette::kResetPressed);
  auto* const reset_gradient_pressed = VSTGUI::CGradient::create(
      0, 1, palette::kReset, palette::kReset);
  result.reset->setGradient(reset_gradient);
  result.reset->setGradientHighlighted(reset_gradient_pressed);
  reset_gradient->forget();
  reset_gradient_pressed->forget();
  result.reset->setRoundRadius(3.0);
  result.reset->setFrameWidth(1.0);
  result.reset->setTextMargin(1.0);
  result.reset->setTextAlignment(VSTGUI::kCenterText);
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
