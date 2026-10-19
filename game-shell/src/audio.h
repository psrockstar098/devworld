#pragma once
// audio.h — procedural environmental audio (wind, pad, crickets, room hum).
// No audio files: everything is synthesized in the SDL audio callback, so the
// WASM build stays small and every city sounds alive without asset licensing.

#include <SDL2/SDL.h>
#include <atomic>
#include <string>
#include "ambience.h"

namespace dw {

class AmbienceAudio {
 public:
  ~AmbienceAudio() { close(); }

  bool open();          // safe to call if audio is unavailable: returns false
  void close();
  void setScene(const std::string& cityId, bool interior, float dayT);

 private:
  static void callback(void* user, Uint8* stream, int len);
  void render(Sint16* out, int frames);

  SDL_AudioDeviceID dev_ = 0;
  std::atomic<float> target_[4]{{0.f}, {0.f}, {0.f}, {0.f}};
  float cur_[4] = {0.f, 0.f, 0.f, 0.f};
  uint32_t rng_ = 0x9e3779b9u;
  double t_ = 0.0;
  float noise_ = 0.0f;
  double sampleRate_ = 22050.0;
};

}  // namespace dw
