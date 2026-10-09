// Síntesis de los efectos de sonido (todo propio, CC0): ruido filtrado, osciladores, formantes y
// envolventes. Cada efecto tiene varias variantes (otra semilla, algo de tono) para no repetirse.
#include <algorithm>
#include <cmath>
#include <numbers>

#include "client/audio.h"
#include "core/random.h"

namespace mcw {
namespace {

constexpr float kTau = 2.0f * std::numbers::pi_v<float>;

struct Synth {
  int sr;
  Random rng;
  std::vector<float> out;
  Synth(int sampleRate, u64 seed, float seconds) : sr(sampleRate), rng(seed), out(static_cast<std::size_t>(seconds * sampleRate), 0.0f) {}
  float noise() { return rng.nextFloat() * 2.0f - 1.0f; }
  float t(std::size_t i) const { return static_cast<float>(i) / static_cast<float>(sr); }
  std::size_t at(float seconds) const { return static_cast<std::size_t>(std::max(0.0f, seconds) * sr); }
};

/// Filtro paso bajo de un polo.
struct LowPass {
  float y = 0, a = 1;
  void set(float fc, int sr) { a = 1.0f - std::exp(-kTau * fc / static_cast<float>(sr)); }
  float operator()(float x) { return y += a * (x - y); }
};

/// Paso banda resonante (biquad).
struct BandPass {
  float b0 = 0, b2 = 0, a1 = 0, a2 = 0, x1 = 0, x2 = 0, y1 = 0, y2 = 0;
  void set(float f, float q, int sr) {
    const float w = kTau * f / static_cast<float>(sr), alpha = std::sin(w) / (2.0f * q), a0 = 1.0f + alpha;
    b0 = alpha / a0;
    b2 = -alpha / a0;
    a1 = -2.0f * std::cos(w) / a0;
    a2 = (1.0f - alpha) / a0;
  }
  float operator()(float x) {
    const float y = b0 * x + b2 * x2 - a1 * y1 - a2 * y2;
    x2 = x1;
    x1 = x;
    y2 = y1;
    y1 = y;
    return y;
  }
};

float env(float t, float attack, float decay) {
  if (t < attack) return t / attack;
  return std::exp(-(t - attack) / decay);
}

void normalize(std::vector<float>& v, float peak) {
  float m = 1e-6f;
  for (float s : v) m = std::max(m, std::abs(s));
  for (float& s : v) s *= peak / m;
  // Rampa de 2 ms al final: sin chasquido al cortar
  const std::size_t n = std::min<std::size_t>(v.size(), 88);
  for (std::size_t i = 0; i < n; i++) v[v.size() - 1 - i] *= static_cast<float>(i) / n;
}

/// Ruido filtrado en "granos" (crujidos): grava, tierra, hierba, nieve...
std::vector<float> grainy(int sr, u64 seed, float seconds, float lowCut, float highCut, int grains, float grainMs, float peak) {
  Synth s(sr, seed, seconds);
  LowPass lp, lp2;
  lp.set(highCut, sr);
  lp2.set(lowCut, sr);
  std::vector<float> amp(s.out.size(), 0.0f);
  for (int g = 0; g < grains; g++) {
    const float start = s.rng.nextFloat() * seconds * 0.75f, len = grainMs / 1000.0f * (0.5f + s.rng.nextFloat());
    const float a = 0.4f + s.rng.nextFloat() * 0.6f;
    for (std::size_t i = s.at(start); i < s.at(start + len) && i < amp.size(); i++) {
      const float x = (s.t(i) - start) / len;
      amp[i] = std::max(amp[i], a * std::sin(x * std::numbers::pi_v<float>));
    }
  }
  for (std::size_t i = 0; i < s.out.size(); i++) {
    const float n = lp(s.noise());
    const float hp = n - lp2(n);  // quitar graves por debajo de lowCut
    s.out[i] = hp * amp[i] * env(s.t(i), 0.002f, seconds * 0.5f);
  }
  normalize(s.out, peak);
  return s.out;
}

/// Voz de animal: diente de sierra con contorno de tono, vibrato y dos formantes.
std::vector<float> voice(int sr, u64 seed, float seconds, float f0, float f1, float vibratoHz, float vibratoDepth, float form1,
                         float form2, float attack, float tremoloHz, float breath, float peak) {
  Synth s(sr, seed, seconds);
  BandPass b1, b2;
  b1.set(form1, 3.0f, sr);
  b2.set(form2, 4.0f, sr);
  LowPass lp;
  lp.set(4000, sr);
  float phase = 0;
  for (std::size_t i = 0; i < s.out.size(); i++) {
    const float t = s.t(i), x = t / seconds;
    const float f = f0 + (f1 - f0) * x + std::sin(t * kTau * vibratoHz) * vibratoDepth;
    phase += f / static_cast<float>(sr);
    phase -= std::floor(phase);
    const float saw = phase * 2.0f - 1.0f;
    const float src = saw + s.noise() * breath;
    const float trem = tremoloHz > 0 ? 0.6f + 0.4f * std::sin(t * kTau * tremoloHz) : 1.0f;
    const float e = std::min(1.0f, t / attack) * std::min(1.0f, (seconds - t) / (seconds * 0.35f));
    s.out[i] = lp(b1(src) * 1.2f + b2(src) * 0.8f + saw * 0.08f) * e * trem;
  }
  normalize(s.out, peak);
  return s.out;
}

void append(std::vector<float>& dst, const std::vector<float>& src, std::size_t offset, float gain = 1.0f) {
  if (dst.size() < offset + src.size()) dst.resize(offset + src.size(), 0.0f);
  for (std::size_t i = 0; i < src.size(); i++) dst[offset + i] += src[i] * gain;
}

}  // namespace

std::vector<float> synthesize(Sfx sfx, int variant, int sr) {
  const u64 seed = static_cast<u64>(sfx) * 7919u + static_cast<u64>(variant) * 104729u + 17u;
  const float vp = 1.0f + (variant - 1) * 0.06f;  // cada variante, un poco más aguda o grave
  switch (sfx) {
    case Sfx::DigStone: {
      // Golpe seco con un "clac" medio-agudo
      Synth s(sr, seed, 0.14f);
      BandPass bp, bp2;
      bp.set(2100.0f * vp, 1.4f, sr);
      bp2.set(900.0f * vp, 2.0f, sr);
      for (std::size_t i = 0; i < s.out.size(); i++) {
        const float n = s.noise(), t = s.t(i);
        s.out[i] = (bp(n) * env(t, 0.001f, 0.022f) + bp2(n) * 0.6f * env(t, 0.001f, 0.045f));
      }
      normalize(s.out, 0.55f);
      return s.out;
    }
    case Sfx::DigWood: {
      // Toc hueco: resonancia grave + ruido corto
      Synth s(sr, seed, 0.18f);
      BandPass bp;
      bp.set(520.0f * vp, 6.0f, sr);
      for (std::size_t i = 0; i < s.out.size(); i++) {
        const float t = s.t(i);
        s.out[i] = bp(s.noise()) * env(t, 0.001f, 0.05f) * 2.0f + std::sin(t * kTau * 170.0f * vp) * env(t, 0.001f, 0.035f) * 0.5f;
      }
      normalize(s.out, 0.6f);
      return s.out;
    }
    case Sfx::DigGravel: return grainy(sr, seed, 0.18f, 200, 1400.0f * vp, 16, 7, 0.55f);
    case Sfx::DigGrass: return grainy(sr, seed, 0.16f, 1200, 6000.0f * vp, 14, 6, 0.45f);
    case Sfx::DigSand: return grainy(sr, seed, 0.2f, 300, 2600.0f * vp, 22, 9, 0.4f);
    case Sfx::DigSnow: return grainy(sr, seed, 0.2f, 400, 2000.0f * vp, 12, 12, 0.45f);
    case Sfx::DigCloth: return grainy(sr, seed, 0.15f, 80, 700.0f * vp, 8, 18, 0.45f);
    case Sfx::DigGlass: {
      // Cristal roto: parciales agudos que se apagan rápido + chasquido
      Synth s(sr, seed, 0.45f);
      float freqs[6];
      for (float& f : freqs) f = (2200.0f + s.rng.nextFloat() * 4200.0f) * vp;
      for (std::size_t i = 0; i < s.out.size(); i++) {
        const float t = s.t(i);
        float v = s.noise() * env(t, 0.0005f, 0.012f) * 0.8f;
        for (int k = 0; k < 6; k++) v += std::sin(t * kTau * freqs[k]) * env(t - k * 0.012f, 0.001f, 0.05f + k * 0.02f) * 0.25f * (t > k * 0.012f);
        s.out[i] = v;
      }
      normalize(s.out, 0.5f);
      return s.out;
    }
    case Sfx::Pop: {
      Synth s(sr, seed, 0.09f);
      float ph = 0;
      for (std::size_t i = 0; i < s.out.size(); i++) {
        const float t = s.t(i);
        ph += (500.0f + t * 9000.0f) * vp / static_cast<float>(sr);
        s.out[i] = std::sin(ph * kTau) * env(t, 0.002f, 0.025f);
      }
      normalize(s.out, 0.35f);
      return s.out;
    }
    case Sfx::Hurt:
      return voice(sr, seed, 0.2f, 210 * vp, 130 * vp, 0, 0, 650, 1300, 0.01f, 0, 0.25f, 0.6f);
    case Sfx::Explosion: {
      Synth s(sr, seed, 1.8f);
      LowPass lp, lp2;
      for (std::size_t i = 0; i < s.out.size(); i++) {
        const float t = s.t(i);
        lp.set(std::max(120.0f, 3500.0f * std::exp(-t * 3.0f)), sr);
        lp2.set(90.0f, sr);
        const float n = s.noise();
        s.out[i] = lp(n) * env(t, 0.004f, 0.45f) * 1.4f + lp2(n) * env(t, 0.01f, 0.7f) * 6.0f +
                   std::sin(t * kTau * (48.0f - t * 12.0f)) * env(t, 0.005f, 0.5f) * 0.5f;
      }
      normalize(s.out, 0.95f);
      return s.out;
    }
    case Sfx::Fuse: {
      // Siseo que crece
      Synth s(sr, seed, 1.5f);
      LowPass lp;
      lp.set(3000, sr);
      for (std::size_t i = 0; i < s.out.size(); i++) {
        const float t = s.t(i), n = s.noise();
        const float hp = n - lp(n);
        s.out[i] = hp * std::min(1.0f, t / 1.2f) * (0.8f + 0.2f * std::sin(t * kTau * 23.0f));
      }
      normalize(s.out, 0.4f);
      return s.out;
    }
    case Sfx::Bow: {
      Synth s(sr, seed, 0.35f);
      BandPass bp;
      for (std::size_t i = 0; i < s.out.size(); i++) {
        const float t = s.t(i);
        bp.set(std::max(400.0f, 3200.0f - t * 9000.0f) * vp, 2.0f, sr);
        s.out[i] = bp(s.noise()) * env(t, 0.01f, 0.08f) * 1.5f + std::sin(t * kTau * 210.0f * vp) * env(t, 0.001f, 0.08f) * 0.4f;
      }
      normalize(s.out, 0.5f);
      return s.out;
    }
    case Sfx::ArrowHit: {
      Synth s(sr, seed, 0.12f);
      for (std::size_t i = 0; i < s.out.size(); i++) {
        const float t = s.t(i);
        s.out[i] = std::sin(t * kTau * 160.0f * vp) * env(t, 0.001f, 0.03f) + s.noise() * env(t, 0.0005f, 0.004f) * 0.5f;
      }
      normalize(s.out, 0.5f);
      return s.out;
    }
    case Sfx::Eat: {
      std::vector<float> out;
      for (int k = 0; k < 3; k++) append(out, grainy(sr, seed + k, 0.1f, 500, 2500.0f * vp, 8, 6, 0.5f), static_cast<std::size_t>(k * 0.13f * sr));
      normalize(out, 0.45f);
      return out;
    }
    case Sfx::Burp: return voice(sr, seed, 0.3f, 120 * vp, 95 * vp, 11, 6, 320, 700, 0.02f, 0, 0.3f, 0.5f);
    case Sfx::Orb: {
      // Un "plin" corto que sube de tono
      Synth s(sr, seed, 0.16f);
      for (std::size_t i = 0; i < s.out.size(); i++) {
        const float t = s.t(i);
        const float f = (1500.0f + 1400.0f * std::min(1.0f, t / 0.08f)) * vp;
        s.out[i] = (std::sin(t * kTau * f) + 0.3f * std::sin(t * kTau * f * 2.0f)) * env(t, 0.002f, 0.045f);
      }
      normalize(s.out, 0.5f);
      return s.out;
    }
    case Sfx::LevelUp: {
      // Arpegio ascendente de campanitas: do, mi, sol y do agudo
      Synth s(sr, seed, 0.9f);
      static const float notes[4] = {523.25f, 659.25f, 783.99f, 1046.5f};
      for (int k = 0; k < 4; k++) {
        const float start = static_cast<float>(k) * 0.085f;
        for (std::size_t i = s.at(start); i < s.out.size(); i++) {
          const float t = s.t(i) - start;
          const float f = notes[k] * vp;
          s.out[i] += (std::sin(t * kTau * f) + 0.35f * std::sin(t * kTau * f * 2.01f) + 0.12f * std::sin(t * kTau * f * 3.0f)) *
                      env(t, 0.003f, k == 3 ? 0.22f : 0.09f) * (k == 3 ? 1.0f : 0.7f);
        }
      }
      normalize(s.out, 0.5f);
      return s.out;
    }
    case Sfx::Enchant: {
      // Un destello mágico: cinco campanitas que suben en espiral sobre un chisporroteo corto
      Synth s(sr, seed, 1.1f);
      for (std::size_t i = 0; i < s.out.size(); i++) {
        const float t = s.t(i);
        float v = s.noise() * env(t, 0.001f, 0.03f) * 0.12f;
        for (int k = 0; k < 5; k++) {
          const float start = static_cast<float>(k) * 0.075f;
          if (t < start) continue;
          const float tt = t - start;
          const float f = (620.0f + 310.0f * static_cast<float>(k) + 700.0f * std::min(1.0f, tt / 0.45f)) * vp;
          v += (std::sin(tt * kTau * f) + 0.3f * std::sin(tt * kTau * f * 2.0f)) * env(tt, 0.004f, 0.28f) * (0.55f / (1.0f + 0.25f * static_cast<float>(k)));
        }
        s.out[i] = v;
      }
      normalize(s.out, 0.5f);
      return s.out;
    }
    case Sfx::Note: {
      // Piano suave: fundamental y armónicos que se apagan antes, con un leve desafinado (coro)
      Synth s(sr, seed, 2.6f);
      const float f0 = 261.63f;
      for (std::size_t i = 0; i < s.out.size(); i++) {
        const float t = s.t(i);
        const float a = std::min(1.0f, t / 0.006f);
        float v = std::sin(t * kTau * f0) * std::exp(-t / 0.9f);
        v += std::sin(t * kTau * f0 * 1.003f) * std::exp(-t / 0.8f) * 0.5f;
        v += std::sin(t * kTau * f0 * 2.0f) * std::exp(-t / 0.35f) * 0.35f;
        v += std::sin(t * kTau * f0 * 3.01f) * std::exp(-t / 0.18f) * 0.12f;
        v += std::sin(t * kTau * f0 * 4.02f) * std::exp(-t / 0.1f) * 0.05f;
        s.out[i] = v * a;
      }
      // Cola a cero para que no chasquee al terminar
      for (std::size_t i = s.out.size() - s.at(0.2f); i < s.out.size(); i++)
        s.out[i] *= static_cast<float>(s.out.size() - i) / static_cast<float>(s.at(0.2f));
      normalize(s.out, 0.5f);
      return s.out;
    }
    case Sfx::Click: {
      Synth s(sr, seed, 0.03f);
      for (std::size_t i = 0; i < s.out.size(); i++) {
        const float t = s.t(i);
        s.out[i] = std::sin(t * kTau * 1300.0f) * env(t, 0.0005f, 0.006f) + s.noise() * env(t, 0.0002f, 0.001f) * 0.4f;
      }
      normalize(s.out, 0.3f);
      return s.out;
    }
    case Sfx::Splash: {
      Synth s(sr, seed, 0.5f);
      LowPass lp;
      for (std::size_t i = 0; i < s.out.size(); i++) {
        const float t = s.t(i);
        lp.set(std::max(400.0f, 2500.0f - t * 4000.0f) * vp, sr);
        s.out[i] = lp(s.noise()) * env(t, 0.01f, 0.15f);
      }
      normalize(s.out, 0.45f);
      return s.out;
    }
    // --- Criaturas ---
    case Sfx::PigSay: {
      // Dos gruñidos nasales cortos
      std::vector<float> out;
      append(out, voice(sr, seed, 0.14f, 260 * vp, 200 * vp, 0, 0, 900, 1500, 0.01f, 0, 0.35f, 0.5f), 0);
      append(out, voice(sr, seed + 1, 0.12f, 240 * vp, 190 * vp, 0, 0, 900, 1500, 0.01f, 0, 0.35f, 0.4f), static_cast<std::size_t>(0.19f * sr));
      return out;
    }
    case Sfx::PigHurt: return voice(sr, seed, 0.25f, 650 * vp, 480 * vp, 18, 25, 1400, 2500, 0.01f, 0, 0.2f, 0.55f);
    case Sfx::CowSay: return voice(sr, seed, 1.0f, 105 * vp, 122 * vp, 5, 3, 480, 850, 0.12f, 0, 0.12f, 0.6f);
    case Sfx::CowHurt: return voice(sr, seed, 0.35f, 150 * vp, 120 * vp, 7, 5, 520, 900, 0.02f, 0, 0.2f, 0.6f);
    case Sfx::SheepSay: return voice(sr, seed, 0.65f, 330 * vp, 300 * vp, 4, 4, 750, 1500, 0.04f, 17, 0.15f, 0.5f);
    case Sfx::ChickenSay: {
      std::vector<float> out;
      for (int k = 0; k < 3; k++)
        append(out, voice(sr, seed + k, 0.055f, 950 * vp, 700 * vp, 0, 0, 1300, 2600, 0.004f, 0, 0.3f, 0.35f),
               static_cast<std::size_t>(k * 0.09f * sr));
      return out;
    }
    case Sfx::ChickenHurt: return voice(sr, seed, 0.16f, 1250 * vp, 900 * vp, 30, 60, 1600, 3000, 0.005f, 0, 0.5f, 0.45f);
    case Sfx::ZombieSay: return voice(sr, seed, 1.1f, 78 * vp, 70 * vp, 3, 4, 380, 720, 0.15f, 31, 0.6f, 0.6f);
    case Sfx::ZombieHurt: return voice(sr, seed, 0.35f, 125 * vp, 95 * vp, 9, 8, 420, 800, 0.01f, 0, 0.5f, 0.6f);
    case Sfx::SkeletonSay:
    case Sfx::SkeletonHurt: {
      // Traqueteo de huesos
      const bool hurt = sfx == Sfx::SkeletonHurt;
      Synth s(sr, seed, hurt ? 0.3f : 0.5f);
      BandPass bp;
      bp.set(1700.0f * vp, 5.0f, sr);
      const int clicks = hurt ? 9 : 7;
      std::vector<float> starts;
      for (int k = 0; k < clicks; k++) starts.push_back(k * (hurt ? 0.028f : 0.06f) + s.rng.nextFloat() * 0.015f);
      for (std::size_t i = 0; i < s.out.size(); i++) {
        const float t = s.t(i);
        float a = 0;
        for (float st : starts) if (t >= st) a += env(t - st, 0.0005f, 0.006f);
        s.out[i] = bp(s.noise()) * a;
      }
      normalize(s.out, 0.5f);
      return s.out;
    }
    case Sfx::SpiderSay:
    case Sfx::SpiderHurt: {
      // Chasquidos y siseo
      const bool hurt = sfx == Sfx::SpiderHurt;
      Synth s(sr, seed, hurt ? 0.3f : 0.55f);
      BandPass bp;
      bp.set(3200.0f * vp, 2.5f, sr);
      for (std::size_t i = 0; i < s.out.size(); i++) {
        const float t = s.t(i);
        const float chatter = 0.5f + 0.5f * std::sin(t * kTau * (hurt ? 55.0f : 34.0f));
        s.out[i] = bp(s.noise()) * chatter * chatter * env(t, 0.01f, hurt ? 0.12f : 0.25f);
      }
      normalize(s.out, 0.45f);
      return s.out;
    }
    case Sfx::CreeperHurt: return grainy(sr, seed, 0.2f, 150, 1800.0f * vp, 10, 15, 0.5f);
    default: break;
  }
  return std::vector<float>(64, 0.0f);
}

}  // namespace mcw
