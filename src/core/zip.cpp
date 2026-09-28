#include "core/zip.h"

#include <miniz.h>

#include <mutex>

namespace mcw {

struct ZipArchive::Impl {
  mz_zip_archive zip{};
  std::vector<u8> memory;  // mantiene vivos los datos si se abrió desde memoria
  mutable std::mutex mutex;  // miniz no es seguro entre hilos con el mismo archivo
};

ZipArchive::ZipArchive() : impl_(std::make_unique<Impl>()) {}

ZipArchive::~ZipArchive() { mz_zip_reader_end(&impl_->zip); }

std::unique_ptr<ZipArchive> ZipArchive::openFile(const std::string& path) {
  std::unique_ptr<ZipArchive> z(new ZipArchive());
  if (!mz_zip_reader_init_file(&z->impl_->zip, path.c_str(), 0)) return nullptr;
  return z;
}

std::unique_ptr<ZipArchive> ZipArchive::openMemory(std::vector<u8> data) {
  std::unique_ptr<ZipArchive> z(new ZipArchive());
  z->impl_->memory = std::move(data);
  if (!mz_zip_reader_init_mem(&z->impl_->zip, z->impl_->memory.data(), z->impl_->memory.size(), 0)) return nullptr;
  return z;
}

bool ZipArchive::contains(const std::string& name) const {
  std::lock_guard lock(impl_->mutex);
  return mz_zip_reader_locate_file(&impl_->zip, name.c_str(), nullptr, 0) >= 0;
}

std::optional<std::vector<u8>> ZipArchive::read(const std::string& name) const {
  std::lock_guard lock(impl_->mutex);
  const int index = mz_zip_reader_locate_file(&impl_->zip, name.c_str(), nullptr, 0);
  if (index < 0) return std::nullopt;
  mz_zip_archive_file_stat st;
  if (!mz_zip_reader_file_stat(&impl_->zip, static_cast<mz_uint>(index), &st)) return std::nullopt;
  std::vector<u8> out(static_cast<std::size_t>(st.m_uncomp_size));
  if (!mz_zip_reader_extract_to_mem(&impl_->zip, static_cast<mz_uint>(index), out.data(), out.size(), 0)) return std::nullopt;
  return out;
}

std::vector<std::string> ZipArchive::list(const std::string& prefix) const {
  std::lock_guard lock(impl_->mutex);
  std::vector<std::string> names;
  const mz_uint n = mz_zip_reader_get_num_files(&impl_->zip);
  char buf[1024];
  for (mz_uint i = 0; i < n; i++) {
    if (mz_zip_reader_is_file_a_directory(&impl_->zip, i)) continue;
    mz_zip_reader_get_filename(&impl_->zip, i, buf, sizeof(buf));
    std::string name(buf);
    if (name.compare(0, prefix.size(), prefix) == 0) names.push_back(std::move(name));
  }
  return names;
}

std::vector<u8> zlibCompress(const u8* data, std::size_t size, int level) {
  mz_ulong outLen = mz_compressBound(static_cast<mz_ulong>(size));
  std::vector<u8> out(outLen);
  if (mz_compress2(out.data(), &outLen, data, static_cast<mz_ulong>(size), level) != MZ_OK) return {};
  out.resize(outLen);
  return out;
}

std::optional<std::vector<u8>> zlibDecompress(const u8* data, std::size_t size, std::size_t expectedSize) {
  if (expectedSize > 0) {
    std::vector<u8> out(expectedSize);
    mz_ulong outLen = static_cast<mz_ulong>(expectedSize);
    if (mz_uncompress(out.data(), &outLen, data, static_cast<mz_ulong>(size)) != MZ_OK || outLen != expectedSize) return std::nullopt;
    return out;
  }
  // Tamaño desconocido: descomprimir por bloques con la API de streaming.
  mz_stream s{};
  if (mz_inflateInit(&s) != MZ_OK) return std::nullopt;
  std::vector<u8> out;
  u8 chunk[16384];
  s.next_in = data;
  s.avail_in = static_cast<unsigned int>(size);
  int status;
  do {
    s.next_out = chunk;
    s.avail_out = sizeof(chunk);
    status = mz_inflate(&s, MZ_NO_FLUSH);
    if (status != MZ_OK && status != MZ_STREAM_END) { mz_inflateEnd(&s); return std::nullopt; }
    out.insert(out.end(), chunk, chunk + (sizeof(chunk) - s.avail_out));
  } while (status != MZ_STREAM_END && (s.avail_in > 0 || s.avail_out == 0));
  mz_inflateEnd(&s);
  if (status != MZ_STREAM_END) return std::nullopt;
  return out;
}

namespace {

/// Deflate sin cabecera (lo que va dentro de un .gz).
std::optional<std::vector<u8>> rawInflate(const u8* data, std::size_t size, std::size_t* consumed) {
  mz_stream s{};
  if (mz_inflateInit2(&s, -MZ_DEFAULT_WINDOW_BITS) != MZ_OK) return std::nullopt;
  std::vector<u8> out;
  u8 chunk[16384];
  s.next_in = data;
  s.avail_in = static_cast<unsigned int>(size);
  int status;
  do {
    s.next_out = chunk;
    s.avail_out = sizeof(chunk);
    status = mz_inflate(&s, MZ_NO_FLUSH);
    if (status != MZ_OK && status != MZ_STREAM_END) { mz_inflateEnd(&s); return std::nullopt; }
    out.insert(out.end(), chunk, chunk + (sizeof(chunk) - s.avail_out));
  } while (status != MZ_STREAM_END && (s.avail_in > 0 || s.avail_out == 0));
  if (consumed) *consumed = size - s.avail_in;
  mz_inflateEnd(&s);
  if (status != MZ_STREAM_END) return std::nullopt;
  return out;
}

}  // namespace

std::vector<u8> zipFiles(const std::vector<std::pair<std::string, std::vector<u8>>>& files) {
  mz_zip_archive z{};
  if (!mz_zip_writer_init_heap(&z, 0, 1 << 16)) return {};
  for (const auto& [name, data] : files) {
    if (!mz_zip_writer_add_mem(&z, name.c_str(), data.data(), data.size(), MZ_DEFAULT_COMPRESSION)) {
      mz_zip_writer_end(&z);
      return {};
    }
  }
  void* buf = nullptr;
  std::size_t size = 0;
  if (!mz_zip_writer_finalize_heap_archive(&z, &buf, &size)) {
    mz_zip_writer_end(&z);
    return {};
  }
  std::vector<u8> out(static_cast<u8*>(buf), static_cast<u8*>(buf) + size);
  mz_zip_writer_end(&z);  // libera buf
  return out;
}

bool isGzip(const u8* data, std::size_t size) { return size >= 18 && data[0] == 0x1F && data[1] == 0x8B && data[2] == 8; }

std::vector<u8> gzipCompress(const u8* data, std::size_t size, int level) {
  // Cabecera de 10 bytes, deflate "crudo", CRC-32 y tamaño (RFC 1952)
  std::vector<u8> out = {0x1F, 0x8B, 8, 0, 0, 0, 0, 0, 0, 255};
  mz_stream s{};
  if (mz_deflateInit2(&s, level, MZ_DEFLATED, -MZ_DEFAULT_WINDOW_BITS, 9, MZ_DEFAULT_STRATEGY) != MZ_OK) return {};
  std::vector<u8> body(mz_deflateBound(&s, static_cast<mz_ulong>(size)) + 16);
  s.next_in = data;
  s.avail_in = static_cast<unsigned int>(size);
  s.next_out = body.data();
  s.avail_out = static_cast<unsigned int>(body.size());
  const int st = mz_deflate(&s, MZ_FINISH);
  const std::size_t n = body.size() - s.avail_out;
  mz_deflateEnd(&s);
  if (st != MZ_STREAM_END) return {};
  out.insert(out.end(), body.begin(), body.begin() + static_cast<std::ptrdiff_t>(n));
  const u32 crc = static_cast<u32>(mz_crc32(MZ_CRC32_INIT, data, size));
  const u32 isize = static_cast<u32>(size);
  for (int i = 0; i < 4; i++) out.push_back(static_cast<u8>(crc >> (8 * i)));
  for (int i = 0; i < 4; i++) out.push_back(static_cast<u8>(isize >> (8 * i)));
  return out;
}

std::optional<std::vector<u8>> gzipDecompress(const u8* data, std::size_t size) {
  if (!isGzip(data, size)) return std::nullopt;
  const u8 flags = data[3];
  std::size_t p = 10;
  if (flags & 4) {  // FEXTRA
    if (p + 2 > size) return std::nullopt;
    p += 2 + (data[p] | (data[p + 1] << 8));
  }
  if (flags & 8) { while (p < size && data[p]) p++; p++; }   // FNAME
  if (flags & 16) { while (p < size && data[p]) p++; p++; }  // FCOMMENT
  if (flags & 2) p += 2;                                     // FHCRC
  if (p >= size) return std::nullopt;
  auto out = rawInflate(data + p, size - p, nullptr);
  if (!out) return std::nullopt;
  const u32 crc = static_cast<u32>(mz_crc32(MZ_CRC32_INIT, out->data(), out->size()));
  const u8* tail = data + size - 8;
  const u32 want = tail[0] | (tail[1] << 8) | (tail[2] << 16) | (static_cast<u32>(tail[3]) << 24);
  if (crc != want) return std::nullopt;
  return out;
}

}  // namespace mcw
