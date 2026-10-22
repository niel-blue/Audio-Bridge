#pragma once
#include "vst_base_palette.h"
#include "vstgui4/vstgui/lib/ccolor.h"

namespace audio_bridge::vst::ui {
struct Palette {
  VSTGUI::CColor kBackground, kPanelRaised, kControlBackground, kSliderTrack;
  VSTGUI::CColor kBorder, kAccent, kButtonBorder, kReset, kResetPressed;
  VSTGUI::CColor kText, kMuted, kDanger, kKnobTop, kKnobBottom, kPressedText;
  bool light = false;
  static Palette From(bool light) {
    const auto& p = light ? vst_base::kLight : vst_base::kDark;
    const auto c = [](vst_base::Color v) { return VSTGUI::CColor(v.r,v.g,v.b); };
    return {c(p.background),c(p.selection),c(p.dialTrack),c(p.track),
      c(p.border),c(p.accent),c(p.buttonBorder),c(p.action),c(p.selection),
      c(p.text),c(p.mutedText),c(p.danger),c(p.knobGradientTop),c(p.knobGradientBottom),
      c(light ? p.onActionText : p.text),light};
  }
};
inline const Palette kDarkPalette = Palette::From(false);
} // namespace audio_bridge::vst::ui
