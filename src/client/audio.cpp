#include "client/audio.h"

#include <algorithm>
#include <cmath>

#include "core/log.h"
#include "data/blocks.h"

namespace mcw {

Audio::~Audio() {
  if (stream_) SDL_DestroyAudioStream(stream_);
}

bool Audio::init() {
  // Búfer corto (poca latencia); en web algo más largo para no cortarse si un frame tarda
#ifdef __EMSCRIPTEN__
  SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, "2048");
#else
  SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, "512");
#endif
  if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
    log::warn("sin audio: {}", SDL_GetError());
    return false;
  }
  SDL_AudioSpec spec{SDL_AUDIO_F32, 2, rate_};
  stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, &Audio::callback, this);
  if (!stream_) {
    log::warn("no se pudo abrir el audio: {}", SDL_GetError());
    return false;
  }
  const u64 t0 = SDL_GetTicksNS();
  std::size_t samples = 0;
  for (int s = 0; s < static_cast<int>(Sfx::Count); s++) {
    for (int v = 0; v < (s == static_cast<int>(Sfx::Note) ? 1 : 3); v++) {
      sounds_[s].push_back(synthesize(static_cast<Sfx>(s), v, rate_));
      samples += sounds_[s].back().size();
    }
  }
  log::info("sonidos: {} efectos sintetizados ({:.1f} s de audio) en {:.0f} ms", static_cast<int>(Sfx::Count),
            samples / static_cast<double>(rate_), (SDL_GetTicksNS() - t0) / 1e6);
  voices_.reserve(48);
  SDL_ResumeAudioStreamDevice(stream_);
  return true;
}

void Audio::setListener(const glm::dvec3& pos, float yaw) {
  listener_ = pos;
  listenerYaw_ = yaw;
}

void Audio::start(Sfx s, float gainL, float gainR, float pitch) {
  auto& variants = sounds_[static_cast<int>(s)];
  if (variants.empty() || (gainL <= 0.001f && gainR <= 0.001f)) return;
  rng_ = rng_ * 1664525u + 1013904223u;
  Voice v;
  v.data = &variants[(rng_ >> 16) % variants.size()];
  v.step = std::clamp(pitch, 0.25f, 4.0f);
  const float g = volume_ * catVolume_[static_cast<int>(categoryOf(s))];
  if (g <= 0.001f) return;
  v.gainL = gainL * g;
  v.gainR = gainR * g;
  std::lock_guard lock(mutex_);
  if (voices_.size() >= 40) voices_.erase(voices_.begin());  // se corta el más antiguo
  voices_.push_back(v);
}

void Audio::play(Sfx s, const glm::dvec3& pos, float volume, float pitch) {
  const glm::dvec3 rel = pos - listener_;
  const double dist = glm::length(rel);
  const double range = 16.0 * std::max(1.0f, volume);
  if (dist > range) return;
  // Los subtítulos se apuntan aunque no haya salida de audio
  if (heard_.size() < 64) heard_.push_back({s, pos, true});
  if (!stream_) return;
  const float gain = std::min(1.0f, volume) * static_cast<float>(1.0 - dist / range);
  // Panorama: proyección sobre el eje derecho del oyente (yaw 0 = mirando a -Z, derecha = +X)
  float pan = 0;
  if (dist > 0.5) {
    const glm::dvec3 right(std::cos(listenerYaw_), 0.0, -std::sin(listenerYaw_));
    pan = static_cast<float>(glm::dot(rel / dist, right)) * 0.8f;
  }
  const float l = std::sqrt((1.0f - pan) * 0.5f), r = std::sqrt((1.0f + pan) * 0.5f);
  start(s, gain * l * 1.41f, gain * r * 1.41f, pitch);
}

void Audio::playFlat(Sfx s, float volume, float pitch) {
  if (heard_.size() < 64) heard_.push_back({s, listener_, false});
  if (!stream_) return;
  start(s, volume, volume, pitch);
}


SoundCategory categoryOf(Sfx s) {
  switch (s) {
    case Sfx::DigStone: case Sfx::DigWood: case Sfx::DigGravel: case Sfx::DigGrass: case Sfx::DigSand: case Sfx::DigGlass:
    case Sfx::DigCloth: case Sfx::DigSnow: case Sfx::Explosion: case Sfx::Splash:
      return SoundCategory::Blocks;
    case Sfx::Pop: case Sfx::Hurt: case Sfx::Bow: case Sfx::ArrowHit: case Sfx::Eat: case Sfx::Burp: case Sfx::Orb: case Sfx::LevelUp:
      return SoundCategory::Players;
    case Sfx::Click: return SoundCategory::Ui;
    case Sfx::Note: return SoundCategory::Music;
    default: return SoundCategory::Mobs;
  }
}

const char* sfxSubtitle(Sfx s) {
  // Sin tildes ni eñes: la fuente del juego (ascii.png) solo tiene ASCII
  switch (s) {
    case Sfx::DigStone: return "Piedra";
    case Sfx::DigWood: return "Madera";
    case Sfx::DigGravel: return "Grava";
    case Sfx::DigGrass: return "Hierba";
    case Sfx::DigSand: return "Arena";
    case Sfx::DigGlass: return "Cristal";
    case Sfx::DigCloth: return "Lana";
    case Sfx::DigSnow: return "Nieve";
    case Sfx::Pop: return "Objeto recogido";
    case Sfx::Hurt: return "Jugador herido";
    case Sfx::Explosion: return "Explosion";
    case Sfx::Fuse: return "Creeper sisea";
    case Sfx::Bow: return "Disparo de arco";
    case Sfx::ArrowHit: return "Flecha clavada";
    case Sfx::Eat: return "Comiendo";
    case Sfx::Burp: return "Eructo";
    case Sfx::Orb: return "Experiencia recogida";
    case Sfx::LevelUp: return "Subes de nivel";
    case Sfx::Splash: return "Chapoteo";
    case Sfx::PigSay: return "Cerdo grune";
    case Sfx::PigHurt: return "Cerdo herido";
    case Sfx::CowSay: return "Vaca muge";
    case Sfx::CowHurt: return "Vaca herida";
    case Sfx::SheepSay: return "Oveja bala";
    case Sfx::ChickenSay: return "Gallina cacarea";
    case Sfx::ChickenHurt: return "Gallina herida";
    case Sfx::ZombieSay: return "Zombi grune";
    case Sfx::ZombieHurt: return "Zombi herido";
    case Sfx::SkeletonSay: return "Esqueleto traquetea";
    case Sfx::SkeletonHurt: return "Esqueleto herido";
    case Sfx::SpiderSay: return "Arana sisea";
    case Sfx::SpiderHurt: return "Arana herida";
    case Sfx::CreeperHurt: return "Creeper herido";
    default: return nullptr;
  }
}

void SDLCALL Audio::callback(void* user, SDL_AudioStream* stream, int additional, int /*total*/) {
  auto* self = static_cast<Audio*>(user);
  const int frames = additional / static_cast<int>(sizeof(float) * 2);
  if (frames <= 0) return;
  self->mixBuf_.assign(static_cast<std::size_t>(frames) * 2, 0.0f);
  self->mix(self->mixBuf_.data(), frames);
  SDL_PutAudioStreamData(stream, self->mixBuf_.data(), frames * static_cast<int>(sizeof(float) * 2));
}

void Audio::mix(float* out, int frames) {
  std::lock_guard lock(mutex_);
  for (Voice& v : voices_) {
    const std::vector<float>& d = *v.data;
    const double n = static_cast<double>(d.size());
    for (int i = 0; i < frames && v.pos < n - 1; i++) {
      const std::size_t k = static_cast<std::size_t>(v.pos);
      const float f = static_cast<float>(v.pos - k);
      const float s = d[k] + (d[k + 1] - d[k]) * f;  // interpolación lineal (cambio de tono)
      out[i * 2] += s * v.gainL;
      out[i * 2 + 1] += s * v.gainR;
      v.pos += v.step;
    }
  }
  std::erase_if(voices_, [](const Voice& v) { return v.pos >= static_cast<double>(v.data->size()) - 1; });
  // Limitador suave para que muchos sonidos a la vez no saturen
  for (int i = 0; i < frames * 2; i++) out[i] = std::tanh(out[i]);
}

Sfx blockSound(int id) {
  switch (id) {
    case B::grass: case B::tallgrass: case B::leaves: case B::leaves2: case B::sapling: case B::reeds: case B::vine:
    case B::yellow_flower: case B::red_flower: case B::double_plant: case B::waterlily: case B::deadbush:
    case B::mycelium: case B::brown_mushroom: case B::red_mushroom: case B::cactus:
      return Sfx::DigGrass;
    case B::sand: case B::soul_sand: return Sfx::DigSand;
    case B::gravel: case B::dirt: case B::clay: case 60 /* farmland */: return Sfx::DigGravel;
    case B::glass: case B::stained_glass: case B::ice: case B::glowstone: case B::stained_glass_pane: return Sfx::DigGlass;
    case B::wool: case 171 /* carpet */: case B::sponge: return Sfx::DigCloth;
    case B::snow: case B::snow_layer: return Sfx::DigSnow;
    default: break;
  }
  const std::string_view m = blockInfo(id).material;
  if (m == "wood") return Sfx::DigWood;
  if (m == "dirt") return Sfx::DigGravel;
  if (m == "plant" || m == "leaves") return Sfx::DigGrass;
  if (m == "wool" || m == "web") return Sfx::DigCloth;
  return Sfx::DigStone;
}

}  // namespace mcw
