#include "audio.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace dw {

namespace {
constexpr double kPi = 3.14159265358979323846;

float noiseStep(uint32_t& s) {
  // xorshift32: deterministic, cheap, audio-thread safe.
  s ^= s << 13;
  s ^= s >> 17;
  s ^= s << 5;
  return ((s & 0xFFFF) / 32768.0f) - 1.0f;
}
}  // namespace

bool AmbienceAudio::open() {
  if (dev_) return true;
  if (!SDL_WasInit(SDL_INIT_AUDIO)) {
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0) return false;
  }
  SDL_AudioSpec want{};
  want.freq = (int)sampleRate_;
  want.format = AUDIO_S16SYS;
  want.channels = 1;
  want.samples = 1024;
  want.callback = &AmbienceAudio::callback;
  want.userdata = this;

  SDL_AudioSpec have{};
  dev_ = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
  if (!dev_) return false;
  sampleRate_ = have.freq > 0 ? have.freq : sampleRate_;
  SDL_PauseAudioDevice(dev_, 0);
  return true;
}

void AmbienceAudio::close() {
  if (dev_) {
    SDL_CloseAudioDevice(dev_);
    dev_ = 0;
  }
}

void AmbienceAudio::setScene(const std::string& cityId, bool interior, float dayT) {
  AmbienceLevels l = ambienceFor(dayT, interior, cityId);
  target_[0].store(l.wind, std::memory_order_relaxed);
  target_[1].store(l.pad, std::memory_order_relaxed);
  target_[2].store(l.crickets, std::memory_order_relaxed);
  target_[3].store(l.hum, std::memory_order_relaxed);
}

void AmbienceAudio::callback(void* user, Uint8* stream, int len) {
  auto* self = static_cast<AmbienceAudio*>(user);
  int frames = len / (int)sizeof(Sint16);
  self->render(reinterpret_cast<Sint16*>(stream), frames);
}

void AmbienceAudio::render(Sint16* out, int frames) {
  const float tw = target_[0].load(std::memory_order_relaxed);
  const float tp = target_[1].load(std::memory_order_relaxed);
  const float tc = target_[2].load(std::memory_order_relaxed);
  const float th = target_[3].load(std::memory_order_relaxed);
  const float goals[4] = {tw, tp, tc, th};

  for (int i = 0; i < frames; ++i) {
    // ~60 ms smoothing so scene changes never click.
    for (int k = 0; k < 4; ++k) cur_[k] += (goals[k] - cur_[k]) * 0.0015f;

    t_ += 1.0 / sampleRate_;
    if (t_ > 1000.0) t_ -= 1000.0;  // keep phase math bounded

    // Wind: low-passed white noise.
    float n = noiseStep(rng_);
    noise_ = noise_ * 0.985f + n * 0.015f;
    float wind = noise_ * cur_[0];

    // Warm pad: fifth interval, slow amplitude breathing.
    float breathe = 0.75f + 0.25f * (float)std::sin(2.0 * kPi * 0.07 * t_);
    float pad = (float)(std::sin(2.0 * kPi * 110.0 * t_) * 0.6 +
                        std::sin(2.0 * kPi * 165.0 * t_) * 0.35) *
                cur_[1] * breathe;

    // Crickets: 3 chirps/sec, short bursts, high carrier.
    double chirp = std::fmod(t_ * 3.0, 1.0);          // 0..1 within each chirp slot
    float env = chirp < 0.22 ? (float)(std::sin(kPi * chirp / 0.22)) : 0.0f;
    env *= env;
    float cricket = (float)std::sin(2.0 * kPi * 4200.0 * t_) * env * cur_[2];

    // Room hum: mains-ish 50/100 Hz.
    float hum = (float)(std::sin(2.0 * kPi * 50.0 * t_) * 0.55 +
                        std::sin(2.0 * kPi * 100.0 * t_) * 0.25) *
                cur_[3];

    float mix = wind * 0.55f + pad * 0.22f + cricket * 0.06f + hum * 0.16f;
    mix *= 0.35f;  // headroom
    mix = std::max(-1.0f, std::min(1.0f, mix));
    out[i] = (Sint16)(mix * 32767.0f);
  }
}

}  // namespace dw
