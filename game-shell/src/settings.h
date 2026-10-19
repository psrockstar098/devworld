#pragma once
// settings.h — accessibility + control preferences (pure, serializable).
//
// Values are clamped here, not in the UI, so the rules can be unit-tested and
// the renderer can trust them. Phase 1 stores them in the local save; the
// server profile will own them later ([ARCH] §1).

#include <string>
#include "json.h"

namespace dw {

struct AccessibilitySettings {
  int uiScale = 100;              // percent, 80..130
  int textScale = 100;            // percent, 80..150 (dialogue + panels)
  bool reducedMotion = false;     // disables bob, shimmer, twinkle, shake
  bool colorblindPalette = false; // deuteranopia-safe accents
  bool highContrast = false;      // stronger borders and text
  bool screenShake = false;       // off by default; reducedMotion forces off

  static AccessibilitySettings parse(const JVal& j);
  std::string toJson() const;

  void clamp();                   // enforce ranges and dependent rules
};

}  // namespace dw
