#include "settings.h"

#include <algorithm>

namespace dw {

static int clampInt(int v, int lo, int hi) { return std::max(lo, std::min(hi, v)); }

void AccessibilitySettings::clamp() {
  uiScale = clampInt(uiScale, 80, 130);
  textScale = clampInt(textScale, 80, 150);
  if (reducedMotion) screenShake = false;  // motion preference wins
}

AccessibilitySettings AccessibilitySettings::parse(const JVal& j) {
  AccessibilitySettings s;
  s.uiScale = j.i("uiScale", s.uiScale);
  s.textScale = j.i("textScale", s.textScale);
  s.reducedMotion = j.boolean("reducedMotion", s.reducedMotion);
  s.colorblindPalette = j.boolean("colorblindPalette", s.colorblindPalette);
  s.highContrast = j.boolean("highContrast", s.highContrast);
  s.screenShake = j.boolean("screenShake", s.screenShake);
  s.clamp();
  return s;
}

std::string AccessibilitySettings::toJson() const {
  AccessibilitySettings c = *this;
  c.clamp();
  std::string s = "{";
  s += "\"uiScale\":" + std::to_string(c.uiScale);
  s += ",\"textScale\":" + std::to_string(c.textScale);
  s += ",\"reducedMotion\":" + std::string(c.reducedMotion ? "true" : "false");
  s += ",\"colorblindPalette\":" + std::string(c.colorblindPalette ? "true" : "false");
  s += ",\"highContrast\":" + std::string(c.highContrast ? "true" : "false");
  s += ",\"screenShake\":" + std::string(c.screenShake ? "true" : "false");
  return s + "}";
}

}  // namespace dw
