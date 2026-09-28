#include "save/region.h"

#include <chrono>
#include <cstring>
#include <format>

#include "core/log.h"
#include "core/zip.h"

namespace mcw {
namespace {

constexpr int kSector = 4096;

u32 be32(const u8* p) { return (u32(p[0]) << 24) | (u32(p[1]) << 16) | (u32(p[2]) << 8) | u32(p[3]); }
void putBe32(u8* p, u32 v) {
  p[0] = static_cast<u8>(v >> 24);
  p[1] = static_cast<u8>(v >> 16);
  p[2] = static_cast<u8>(v >> 8);
  p[3] = static_cast<u8>(v);
}

}  // namespace

RegionFile::RegionFile(std::filesystem::path path) : path_(std::move(path)) {}

bool RegionFile::open(bool create) {
  if (opened_) return true;
  std::error_code ec;
  const bool exists = std::filesystem::exists(path_, ec);
  if (!exists && !create) return false;
  if (!exists) {
    std::filesystem::create_directories(path_.parent_path(), ec);
    std::ofstream(path_, std::ios::binary).write(std::string(kSector * 2, '\0').data(), kSector * 2);
  }
  file_.open(path_, std::ios::in | std::ios::out | std::ios::binary);
  if (!file_) {
    log::warn("no se pudo abrir la región {}", path_.string());
    return false;
  }
  file_.seekg(0, std::ios::end);
  const auto size = static_cast<std::size_t>(file_.tellg());
  std::vector<u8> head(kSector * 2, 0);
  file_.seekg(0);
  file_.read(reinterpret_cast<char*>(head.data()), static_cast<std::streamsize>(std::min<std::size_t>(size, head.size())));
  file_.clear();
  const std::size_t sectors = std::max<std::size_t>(2, (size + kSector - 1) / kSector);
  used_.assign(sectors, false);
  used_[0] = used_[1] = true;
  for (int i = 0; i < 1024; i++) {
    offsets_[i] = be32(&head[static_cast<std::size_t>(i) * 4]);
    timestamps_[i] = be32(&head[kSector + static_cast<std::size_t>(i) * 4]);
    const u32 start = offsets_[i] >> 8, count = offsets_[i] & 0xFF;
    if (offsets_[i] == 0) continue;
    if (start < 2 || start + count > sectors) {  // entrada rota: se ignora
      offsets_[i] = 0;
      continue;
    }
    for (u32 s = start; s < start + count; s++) used_[s] = true;
  }
  opened_ = true;
  return true;
}

void RegionFile::writeHeader(int index) {
  u8 b[4];
  putBe32(b, offsets_[index]);
  file_.seekp(static_cast<std::streamoff>(index) * 4);
  file_.write(reinterpret_cast<const char*>(b), 4);
  putBe32(b, timestamps_[index]);
  file_.seekp(kSector + static_cast<std::streamoff>(index) * 4);
  file_.write(reinterpret_cast<const char*>(b), 4);
}

std::optional<std::vector<u8>> RegionFile::read(int lx, int lz) {
  if (!open(false)) return std::nullopt;
  const int index = lx + lz * 32;
  const u32 off = offsets_[index];
  if (off == 0) return std::nullopt;
  const u32 start = off >> 8, count = off & 0xFF;
  std::vector<u8> buf(static_cast<std::size_t>(count) * kSector);
  file_.seekg(static_cast<std::streamoff>(start) * kSector);
  file_.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(buf.size()));
  file_.clear();
  const u32 len = be32(buf.data());
  if (len < 1 || len + 4 > buf.size()) return std::nullopt;
  const u8 compression = buf[4];
  const u8* data = buf.data() + 5;
  const std::size_t n = len - 1;
  if (compression == 2) return zlibDecompress(data, n);
  if (compression == 1) return gzipDecompress(data, n);
  return std::nullopt;
}

bool RegionFile::write(int lx, int lz, const std::vector<u8>& nbt) {
  if (!open(true)) return false;
  const int index = lx + lz * 32;
  const std::vector<u8> packed = zlibCompress(nbt.data(), nbt.size());
  if (packed.empty()) return false;
  const std::size_t total = packed.size() + 5;
  const u32 need = static_cast<u32>((total + kSector - 1) / kSector);
  if (need >= 256) {
    log::warn("chunk demasiado grande para la región ({} bytes)", total);
    return false;
  }
  // Liberar los sectores viejos y buscar un hueco (o ponerlo al final)
  const u32 oldStart = offsets_[index] >> 8, oldCount = offsets_[index] & 0xFF;
  for (u32 s = oldStart; s < oldStart + oldCount && s < used_.size(); s++) used_[s] = false;
  u32 start = 0;
  for (u32 s = 2, run = 0; s < used_.size(); s++) {
    run = used_[s] ? 0 : run + 1;
    if (run == need) {
      start = s - need + 1;
      break;
    }
  }
  if (start == 0) {
    start = static_cast<u32>(used_.size());
    used_.resize(used_.size() + need, false);
  }
  for (u32 s = start; s < start + need; s++) used_[s] = true;

  std::vector<u8> out(static_cast<std::size_t>(need) * kSector, 0);
  putBe32(out.data(), static_cast<u32>(packed.size() + 1));
  out[4] = 2;  // zlib
  std::memcpy(out.data() + 5, packed.data(), packed.size());
  file_.seekp(static_cast<std::streamoff>(start) * kSector);
  file_.write(reinterpret_cast<const char*>(out.data()), static_cast<std::streamsize>(out.size()));
  offsets_[index] = (start << 8) | need;
  timestamps_[index] = static_cast<u32>(std::chrono::duration_cast<std::chrono::seconds>(
                                            std::chrono::system_clock::now().time_since_epoch()).count());
  writeHeader(index);
  file_.flush();
  return static_cast<bool>(file_);
}

RegionFile& RegionStore::region(int rx, int rz) {
  const i64 key = (static_cast<i64>(rx) << 32) ^ static_cast<u32>(rz);
  auto& r = regions_[key];
  if (!r) r = std::make_unique<RegionFile>(dir_ / std::format("r.{}.{}.mca", rx, rz));
  return *r;
}

std::optional<std::vector<u8>> RegionStore::readChunk(int cx, int cz) {
  return region(cx >> 5, cz >> 5).read(cx & 31, cz & 31);
}

bool RegionStore::writeChunk(int cx, int cz, const std::vector<u8>& nbt) {
  return region(cx >> 5, cz >> 5).write(cx & 31, cz & 31, nbt);
}

bool RegionStore::hasChunk(int cx, int cz) {
  RegionFile& r = region(cx >> 5, cz >> 5);
  return r.read(cx & 31, cz & 31).has_value();
}

}  // namespace mcw
