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

}  // namespace mcw
