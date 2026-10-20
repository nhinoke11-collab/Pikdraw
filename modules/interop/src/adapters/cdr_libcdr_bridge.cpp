// C ABI bridge built with the same MinGW C++ ABI as the pinned libcdr package.
// The editor-side worker loads this DLL dynamically and only exchanges byte buffers.

#include <libcdr/libcdr.h>
#include <librevenge/librevenge.h>
#include <librevenge-generators/librevenge-generators.h>
#include <librevenge-stream/librevenge-stream.h>
#include <zlib.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

constexpr size_t kMaxZipEntries = UINT16_MAX;
constexpr size_t kMaxRawSvgBytes = UINT32_MAX;
constexpr size_t kMaxCompactSvgBytes = UINT32_MAX;
constexpr size_t kMaxUniqueImageBytes = UINT32_MAX;
constexpr size_t kMaxCdrPacketBytes = UINT32_MAX;
constexpr size_t kMaxImageInstances = UINT32_MAX;
uint16_t ReadU16(const uint8_t* bytes) noexcept {
  return static_cast<uint16_t>(bytes[0] | (static_cast<uint16_t>(bytes[1]) << 8));
}

uint32_t ReadU32(const uint8_t* bytes) noexcept {
  return static_cast<uint32_t>(bytes[0]) |
         (static_cast<uint32_t>(bytes[1]) << 8) |
         (static_cast<uint32_t>(bytes[2]) << 16) |
         (static_cast<uint32_t>(bytes[3]) << 24);
}

void SetDiagnostic(char* output, uint32_t capacity, const char* value) noexcept {
  if (!output || capacity == 0) return;
  const size_t length = (std::min)(std::strlen(value), static_cast<size_t>(capacity - 1));
  std::memcpy(output, value, length);
  output[length] = '\0';
}

class MemoryInput : public librevenge::RVNGInputStream {
 public:
  MemoryInput(const uint8_t* bytes, size_t length) : data_(bytes), size_(length) {}
  explicit MemoryInput(std::vector<uint8_t> bytes) : owned_(std::move(bytes)) {
    data_ = owned_.data();
    size_ = owned_.size();
  }

  const unsigned char* read(unsigned long requested, unsigned long& read_count) override {
    const size_t remaining = size_ - position_;
    const size_t count = (std::min)(remaining, static_cast<size_t>(requested));
    read_count = static_cast<unsigned long>(count);
    if (count == 0) return nullptr;
    const unsigned char* result = data_ + position_;
    position_ += count;
    return result;
  }

  long tell() override { return static_cast<long>(position_); }

  int seek(long offset, librevenge::RVNG_SEEK_TYPE type) override {
    int64_t base = 0;
    switch (type) {
      case librevenge::RVNG_SEEK_SET: base = 0; break;
      case librevenge::RVNG_SEEK_CUR: base = static_cast<int64_t>(position_); break;
      case librevenge::RVNG_SEEK_END: base = static_cast<int64_t>(size_); break;
      default: return -1;
    }
    const int64_t target = base + static_cast<int64_t>(offset);
    if (target < 0 || static_cast<uint64_t>(target) > size_) return -1;
    position_ = static_cast<size_t>(target);
    return 0;
  }

  bool isEnd() override { return position_ >= size_; }
  bool isStructured() override { return false; }
  unsigned subStreamCount() override { return 0; }
  const char* subStreamName(unsigned) override { return nullptr; }
  bool existsSubStream(const char*) override { return false; }
  librevenge::RVNGInputStream* getSubStreamByName(const char*) override { return nullptr; }
  librevenge::RVNGInputStream* getSubStreamById(unsigned) override { return nullptr; }

 protected:
  std::vector<uint8_t> owned_;
  const uint8_t* data_{nullptr};
  size_t size_{0};
  size_t position_{0};
};

struct ZipEntry {
  std::string name;
  uint16_t flags{0};
  uint16_t method{0};
  uint32_t crc32{0};
  uint32_t compressed_size{0};
  uint32_t expanded_size{0};
  uint32_t local_header_offset{0};
};

class ZipInput final : public MemoryInput {
 public:
  ZipInput(const uint8_t* bytes, size_t length) : MemoryInput(bytes, length) {}

  bool Initialize() {
    if (size_ < 22) return Fail("CDR ZIP container is truncated");
    const size_t search_size = (std::min)(size_, size_t{65536 + 22});
    const size_t search_start = size_ - search_size;
    size_t eocd = std::numeric_limits<size_t>::max();
    for (size_t i = size_ - 22;; --i) {
      if (ReadU32(data_ + i) == 0x06054b50u) { eocd = i; break; }
      if (i == search_start) break;
    }
    if (eocd == std::numeric_limits<size_t>::max() || eocd + 22 > size_)
      return Fail("CDR ZIP directory is missing or corrupt");
    const uint8_t* end = data_ + eocd;
    if (ReadU16(end + 4) != 0 || ReadU16(end + 6) != 0 ||
        ReadU16(end + 8) != ReadU16(end + 10))
      return Fail("Multi-disk CDR ZIP files are not supported");
    const uint16_t count = ReadU16(end + 10);
    const uint32_t directory_size = ReadU32(end + 12);
    const uint32_t directory_offset = ReadU32(end + 16);
    if (count == 0xffff || directory_size == 0xffffffffu ||
        directory_offset == 0xffffffffu || count > kMaxZipEntries ||
        static_cast<uint64_t>(directory_offset) + directory_size > eocd)
      return Fail("CDR ZIP64 or oversized directory is outside the safe profile");

    size_t offset = directory_offset;
    const size_t directory_end = offset + directory_size;
    entries_.reserve(count);
    for (uint16_t index = 0; index < count; ++index) {
      if (offset + 46 > directory_end || ReadU32(data_ + offset) != 0x02014b50u)
        return Fail("CDR ZIP central directory is malformed");
      const uint8_t* header = data_ + offset;
      ZipEntry entry;
      entry.flags = ReadU16(header + 8);
      entry.method = ReadU16(header + 10);
      entry.crc32 = ReadU32(header + 16);
      entry.compressed_size = ReadU32(header + 20);
      entry.expanded_size = ReadU32(header + 24);
      const uint16_t name_length = ReadU16(header + 28);
      const uint16_t extra_length = ReadU16(header + 30);
      const uint16_t comment_length = ReadU16(header + 32);
      entry.local_header_offset = ReadU32(header + 42);
      const uint64_t record_end = static_cast<uint64_t>(offset) + 46 + name_length +
                                  extra_length + comment_length;
      if (record_end > directory_end || name_length == 0 ||
          entry.compressed_size == 0xffffffffu || entry.expanded_size == 0xffffffffu ||
          entry.local_header_offset == 0xffffffffu)
        return Fail("CDR ZIP entry has an invalid or ZIP64 size");
      entry.name.assign(reinterpret_cast<const char*>(header + 46), name_length);
      if (entry.name.find('\0') != std::string::npos || entry.name.find('\\') != std::string::npos ||
          entry.name.front() == '/' || entry.name.find("../") != std::string::npos ||
          entry.name == ".." || entry.name.find(":") != std::string::npos)
        return Fail("CDR ZIP contains an unsafe entry name");
      if ((entry.flags & 0x0001u) != 0) return Fail("Encrypted CDR ZIP entries are not supported");
      if (entry.method != 0 && entry.method != 8)
        return Fail("CDR ZIP compression method is unsupported");
      for (const auto& existing : entries_) {
        if (existing.name == entry.name) return Fail("CDR ZIP contains duplicate entry names");
      }
      entries_.push_back(std::move(entry));
      offset = static_cast<size_t>(record_end);
    }
    if (offset != directory_end) return Fail("CDR ZIP directory has trailing or malformed records");
    return true;
  }

  bool isStructured() override { return true; }
  unsigned subStreamCount() override { return static_cast<unsigned>(entries_.size()); }
  const char* subStreamName(unsigned index) override {
    return index < entries_.size() ? entries_[index].name.c_str() : nullptr;
  }
  bool existsSubStream(const char* name) override { return Find(name) != nullptr; }
  librevenge::RVNGInputStream* getSubStreamByName(const char* name) override {
    const ZipEntry* entry = Find(name);
    if (!entry) return nullptr;
    std::vector<uint8_t> content;
    if (!Extract(*entry, content)) return nullptr;
    return new (std::nothrow) MemoryInput(std::move(content));
  }
  librevenge::RVNGInputStream* getSubStreamById(unsigned index) override {
    if (index >= entries_.size()) return nullptr;
    return getSubStreamByName(entries_[index].name.c_str());
  }
  const std::string& diagnostic() const noexcept { return diagnostic_; }

 private:
  bool Fail(const char* message) {
    diagnostic_ = message;
    return false;
  }
  const ZipEntry* Find(const char* name) const noexcept {
    if (!name) return nullptr;
    for (const auto& entry : entries_) if (entry.name == name) return &entry;
    return nullptr;
  }
  bool Extract(const ZipEntry& entry, std::vector<uint8_t>& output) {
    if (entry.expanded_size > UINT64_MAX - expanded_bytes_)
      return Fail("CDR ZIP expanded byte count overflowed");
    const size_t local = entry.local_header_offset;
    if (local + 30 > size_ || ReadU32(data_ + local) != 0x04034b50u)
      return Fail("CDR ZIP local header is invalid");
    const uint16_t name_length = ReadU16(data_ + local + 26);
    const uint16_t extra_length = ReadU16(data_ + local + 28);
    const uint64_t data_start64 = static_cast<uint64_t>(local) + 30 + name_length + extra_length;
    const uint64_t data_end64 = data_start64 + entry.compressed_size;
    if (data_end64 > size_ || data_start64 > std::numeric_limits<size_t>::max())
      return Fail("CDR ZIP compressed entry exceeds source bounds");
    const std::string local_name(reinterpret_cast<const char*>(data_ + local + 30), name_length);
    if (local_name != entry.name) return Fail("CDR ZIP central/local entry names disagree");
    const auto* compressed = data_ + static_cast<size_t>(data_start64);
    try { output.resize(entry.expanded_size); }
    catch (...) { return Fail("CDR ZIP entry allocation failed"); }
    if (entry.method == 0) {
      if (entry.compressed_size != entry.expanded_size)
        return Fail("Stored CDR ZIP entry sizes disagree");
      if (!output.empty()) std::memcpy(output.data(), compressed, output.size());
    } else {
      z_stream stream{};
      stream.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(compressed));
      stream.avail_in = entry.compressed_size;
      stream.next_out = reinterpret_cast<Bytef*>(output.data());
      stream.avail_out = entry.expanded_size;
      if (inflateInit2(&stream, -MAX_WBITS) != Z_OK)
        return Fail("CDR ZIP decompressor initialization failed");
      const int status = inflate(&stream, Z_FINISH);
      const bool valid = status == Z_STREAM_END && stream.total_in == entry.compressed_size &&
                         stream.total_out == entry.expanded_size;
      inflateEnd(&stream);
      if (!valid) return Fail("CDR ZIP deflate stream is malformed");
    }
    const uLong checksum = crc32(0L, Z_NULL, 0);
    const uLong actual_crc = output.empty() ? checksum :
        crc32(checksum, reinterpret_cast<const Bytef*>(output.data()), static_cast<uInt>(output.size()));
    if (static_cast<uint32_t>(actual_crc) != entry.crc32)
      return Fail("CDR ZIP entry checksum failed");
    expanded_bytes_ += output.size();
    return true;
  }

  std::vector<ZipEntry> entries_;
  uint64_t expanded_bytes_{0};
  std::string diagnostic_;
};

struct EmbeddedImage {
  std::string mime_type;
  std::vector<uint8_t> bytes;
  uint64_t hash{0};
};

struct ImageInstance {
  uint32_t instance_id{0};
  uint32_t asset_id{0};
};

void AppendU32(std::vector<uint8_t>& output, uint32_t value) {
  output.push_back(static_cast<uint8_t>(value));
  output.push_back(static_cast<uint8_t>(value >> 8));
  output.push_back(static_cast<uint8_t>(value >> 16));
  output.push_back(static_cast<uint8_t>(value >> 24));
}

bool DecodeBase64(std::string_view input, std::vector<uint8_t>& output) {
  output.clear();
  uint32_t accumulator = 0;
  unsigned bit_count = 0;
  bool padding_started = false;
  for (const unsigned char ch : input) {
    if (std::isspace(ch) != 0) continue;
    if (ch == '=') { padding_started = true; continue; }
    if (padding_started) return false;
    int value = -1;
    if (ch >= 'A' && ch <= 'Z') value = ch - 'A';
    else if (ch >= 'a' && ch <= 'z') value = ch - 'a' + 26;
    else if (ch >= '0' && ch <= '9') value = ch - '0' + 52;
    else if (ch == '+') value = 62;
    else if (ch == '/') value = 63;
    else return false;
    accumulator = (accumulator << 6) | static_cast<uint32_t>(value);
    bit_count += 6;
    if (bit_count >= 8) {
      bit_count -= 8;
      output.push_back(static_cast<uint8_t>(accumulator >> bit_count));
      if (output.size() > kMaxUniqueImageBytes) return false;
      accumulator &= bit_count == 0 ? 0u : ((1u << bit_count) - 1u);
    }
  }
  return bit_count == 0 || (bit_count < 6 && accumulator == 0);
}

uint64_t HashBytes(const std::vector<uint8_t>& bytes) noexcept {
  uint64_t value = 1469598103934665603ull;
  for (const uint8_t byte : bytes) {
    value ^= byte;
    value *= 1099511628211ull;
  }
  return value;
}

bool ParseImageTag(std::string_view tag,
                   std::vector<std::pair<std::string_view, std::string_view>>& attributes) {
  attributes.clear();
  size_t offset = std::string_view("<svg:image").size();
  while (offset < tag.size()) {
    while (offset < tag.size() && std::isspace(static_cast<unsigned char>(tag[offset])) != 0) ++offset;
    if (offset == tag.size() || tag[offset] == '>' || tag[offset] == '/') return true;
    const size_t name_begin = offset;
    while (offset < tag.size() &&
           (std::isalnum(static_cast<unsigned char>(tag[offset])) != 0 ||
            tag[offset] == ':' || tag[offset] == '_' || tag[offset] == '-')) ++offset;
    if (name_begin == offset) return false;
    const auto name = tag.substr(name_begin, offset - name_begin);
    while (offset < tag.size() && std::isspace(static_cast<unsigned char>(tag[offset])) != 0) ++offset;
    if (offset == tag.size() || tag[offset++] != '=') return false;
    while (offset < tag.size() && std::isspace(static_cast<unsigned char>(tag[offset])) != 0) ++offset;
    if (offset == tag.size() || (tag[offset] != '\'' && tag[offset] != '"')) return false;
    const char quote = tag[offset++];
    const size_t value_end = tag.find(quote, offset);
    if (value_end == std::string_view::npos) return false;
    attributes.emplace_back(name, tag.substr(offset, value_end - offset));
    offset = value_end + 1;
  }
  return false;
}

std::string_view FindAttribute(
    const std::vector<std::pair<std::string_view, std::string_view>>& attributes,
    std::string_view name) {
  const auto found = std::find_if(attributes.begin(), attributes.end(),
      [&](const auto& attribute) { return attribute.first == name; });
  return found == attributes.end() ? std::string_view{} : found->second;
}

bool CompactSvgImages(std::string_view input, std::vector<uint8_t>& packet,
                      std::string& diagnostic) {
  std::string compact_svg;
  compact_svg.reserve((std::min)(input.size(), size_t{4 * 1024 * 1024}));
  std::vector<EmbeddedImage> assets;
  std::vector<ImageInstance> instances;
  std::unordered_map<uint64_t, std::vector<size_t>> assets_by_hash;
  size_t unique_image_bytes = 0;
  size_t cursor = 0;
  while (cursor < input.size()) {
    const size_t image_begin = input.find("<svg:image", cursor);
    if (image_begin == std::string_view::npos) {
      compact_svg.append(input.substr(cursor));
      break;
    }
    compact_svg.append(input.substr(cursor, image_begin - cursor));
    const size_t image_end = input.find('>', image_begin + 10);
    if (image_end == std::string_view::npos) {
      diagnostic = "libcdr returned a truncated image element";
      return false;
    }
    const std::string_view tag = input.substr(image_begin, image_end + 1 - image_begin);
    std::vector<std::pair<std::string_view, std::string_view>> attributes;
    if (!ParseImageTag(tag, attributes)) {
      diagnostic = "libcdr returned a malformed image element";
      return false;
    }
    const auto href = FindAttribute(attributes, "xlink:href").empty()
        ? FindAttribute(attributes, "href") : FindAttribute(attributes, "xlink:href");
    if (href.substr(0, 5) != "data:") {
      diagnostic = "CDR contains a linked image that cannot be resolved without user-provided source data";
      return false;
    }
    const size_t comma = href.find(',');
    const auto metadata = comma == std::string_view::npos ? std::string_view{} : href.substr(5, comma - 5);
    const size_t semicolon = metadata.find(';');
    if (semicolon == std::string_view::npos || metadata.substr(semicolon + 1) != "base64") {
      diagnostic = "CDR image encoding is outside the bounded embedded-image profile";
      return false;
    }
    const std::string_view mime_type = metadata.substr(0, semicolon);
    if (mime_type != "image/png" && mime_type != "image/jpeg" && mime_type != "image/tiff" &&
        mime_type != "image/bmp") {
      diagnostic = "CDR embedded image media type is outside the bounded profile: " +
          std::string(mime_type);
      return false;
    }
    const auto width = FindAttribute(attributes, "width");
    const auto height = FindAttribute(attributes, "height");
    if (width.empty() || height.empty()) {
      diagnostic = "CDR embedded image is missing its placement dimensions";
      return false;
    }

    std::vector<uint8_t> image_bytes;
    if (!DecodeBase64(href.substr(comma + 1), image_bytes) || image_bytes.empty()) {
      diagnostic = "CDR embedded image has invalid base64 or exceeds the package representation size";
      return false;
    }
    const uint64_t hash = HashBytes(image_bytes);
    uint32_t asset_id = 0;
    const auto bucket = assets_by_hash.find(hash);
    if (bucket != assets_by_hash.end()) {
      for (const size_t candidate : bucket->second) {
        const auto& existing = assets[candidate];
        if (existing.mime_type == mime_type && existing.bytes == image_bytes) {
          asset_id = static_cast<uint32_t>(candidate + 1);
          break;
        }
      }
    }
    if (asset_id == 0) {
      if (image_bytes.size() > kMaxUniqueImageBytes - unique_image_bytes) {
        diagnostic = "CDR embedded image assets exceed the package representation size";
        return false;
      }
      unique_image_bytes += image_bytes.size();
      asset_id = static_cast<uint32_t>(assets.size() + 1);
      assets_by_hash[hash].push_back(assets.size());
      assets.push_back({std::string(mime_type), std::move(image_bytes), hash});
    }
    if (instances.size() >= kMaxImageInstances) {
      diagnostic = "CDR image instance count exceeds the bounded profile";
      return false;
    }
    const uint32_t instance_id = static_cast<uint32_t>(instances.size() + 1);
    instances.push_back({instance_id, asset_id});
    const auto x = FindAttribute(attributes, "x");
    const auto y = FindAttribute(attributes, "y");
    const auto transform = FindAttribute(attributes, "transform");
    const auto clip_path = FindAttribute(attributes, "clip-path");
    if (!clip_path.empty()) {
      compact_svg += "<svg:g clip-path=\"" + std::string(clip_path) + "\"";
      if (!transform.empty()) compact_svg += " transform=\"" + std::string(transform) + "\"";
      compact_svg += "><svg:rect id=\"__pikdraw_cdr_raster_" + std::to_string(instance_id) +
          "\" x=\"" + std::string(x.empty() ? std::string_view("0") : x) +
          "\" y=\"" + std::string(y.empty() ? std::string_view("0") : y) + "\" width=\"" +
          std::string(width) + "\" height=\"" + std::string(height) +
          "\" fill=\"none\" stroke=\"none\"/></svg:g>";
    } else {
      compact_svg += "<svg:rect id=\"__pikdraw_cdr_raster_" + std::to_string(instance_id) +
          "\" x=\"" + std::string(x.empty() ? std::string_view("0") : x) +
          "\" y=\"" + std::string(y.empty() ? std::string_view("0") : y) + "\"";
      if (!transform.empty()) compact_svg += " transform=\"" + std::string(transform) + "\"";
      compact_svg += " width=\"" + std::string(width) + "\" height=\"" + std::string(height) +
          "\" fill=\"none\" stroke=\"none\"/>";
    }
    cursor = image_end + 1;
    size_t before_end = image_end;
    while (before_end > image_begin &&
           std::isspace(static_cast<unsigned char>(input[before_end - 1])) != 0) --before_end;
    const bool self_closing = before_end > image_begin && input[before_end - 1] == '/';
    if (!self_closing) {
      const size_t close = input.find("</svg:image>", cursor);
      if (close == std::string_view::npos) {
        diagnostic = "libcdr returned an unbalanced image element";
        return false;
      }
      cursor = close + std::string_view("</svg:image>").size();
    }
    if (compact_svg.size() > kMaxCompactSvgBytes) {
      diagnostic = "CDR vector markup exceeds the package representation size";
      return false;
    }
  }
  if (input.find("<svg:image") == std::string_view::npos) compact_svg.assign(input);
  if (compact_svg.empty() || compact_svg.size() > kMaxCompactSvgBytes ||
      compact_svg.size() > UINT32_MAX || instances.size() > UINT32_MAX || assets.size() > UINT32_MAX) {
    diagnostic = "CDR compact SVG exceeds the bounded page profile";
    return false;
  }

  size_t packet_size = 16 + compact_svg.size() + instances.size() * 8;
  for (const auto& asset : assets) {
    if (asset.mime_type.size() > UINT32_MAX || asset.bytes.size() > UINT32_MAX ||
        asset.mime_type.size() > kMaxCdrPacketBytes - packet_size - 8 ||
        asset.bytes.size() > kMaxCdrPacketBytes - packet_size - 8 - asset.mime_type.size()) {
      diagnostic = "CDR compact import package exceeds the bounded 128 MiB profile";
      return false;
    }
    packet_size += 8 + asset.mime_type.size() + asset.bytes.size();
  }
  if (packet_size > kMaxCdrPacketBytes || packet_size > UINT32_MAX) {
    diagnostic = "CDR compact import package exceeds the bounded 128 MiB profile";
    return false;
  }
  packet.clear();
  packet.reserve(packet_size);
  AppendU32(packet, 0x31434450u);  // PDC1
  AppendU32(packet, static_cast<uint32_t>(compact_svg.size()));
  AppendU32(packet, static_cast<uint32_t>(instances.size()));
  AppendU32(packet, static_cast<uint32_t>(assets.size()));
  packet.insert(packet.end(), compact_svg.begin(), compact_svg.end());
  for (const auto& instance : instances) {
    AppendU32(packet, instance.instance_id);
    AppendU32(packet, instance.asset_id);
  }
  for (const auto& asset : assets) {
    AppendU32(packet, static_cast<uint32_t>(asset.mime_type.size()));
    AppendU32(packet, static_cast<uint32_t>(asset.bytes.size()));
    packet.insert(packet.end(), asset.mime_type.begin(), asset.mime_type.end());
    packet.insert(packet.end(), asset.bytes.begin(), asset.bytes.end());
  }
  return packet.size() == packet_size;
}

}  // namespace

extern "C" __declspec(dllexport) int __cdecl PikDrawCdrConvertToPackage(
    const uint8_t* source, uint64_t source_size, uint8_t** output,
    uint64_t* output_size, char* diagnostic, uint32_t diagnostic_capacity) noexcept {
  if (output) *output = nullptr;
  if (output_size) *output_size = 0;
  if (diagnostic && diagnostic_capacity) diagnostic[0] = '\0';
  if (!source || source_size == 0 || !output || !output_size || source_size > SIZE_MAX) {
    SetDiagnostic(diagnostic, diagnostic_capacity, "CDR source is empty or exceeds addressable size");
    return 0;
  }
  try {
    std::unique_ptr<MemoryInput> memory;
    std::unique_ptr<ZipInput> zip;
    librevenge::RVNGInputStream* input = nullptr;
    if (source_size >= 4 && source[0] == 'P' && source[1] == 'K' && source[2] == 3 && source[3] == 4) {
      zip.reset(new (std::nothrow) ZipInput(source, static_cast<size_t>(source_size)));
      if (!zip || !zip->Initialize()) {
        SetDiagnostic(diagnostic, diagnostic_capacity,
            zip ? zip->diagnostic().c_str() : "CDR ZIP allocation failed");
        return 0;
      }
      input = zip.get();
    } else {
      if (source_size < 12 || std::memcmp(source, "RIFF", 4) != 0 ||
          std::memcmp(source + 8, "CDR", 3) != 0) {
        SetDiagnostic(diagnostic, diagnostic_capacity, "Source is neither a supported CDR ZIP nor RIFF container");
        return 0;
      }
      memory.reset(new (std::nothrow) MemoryInput(source, static_cast<size_t>(source_size)));
      if (!memory) {
        SetDiagnostic(diagnostic, diagnostic_capacity, "CDR source allocation failed");
        return 0;
      }
      input = memory.get();
    }

    if (!libcdr::CDRDocument::isSupported(input)) {
      SetDiagnostic(diagnostic, diagnostic_capacity,
          zip && !zip->diagnostic().empty() ? zip->diagnostic().c_str() :
          "libcdr does not support this CDR version or container");
      return 0;
    }
    librevenge::RVNGStringVector pages;
    librevenge::RVNGSVGDrawingGenerator painter(pages, "svg");
    if (!libcdr::CDRDocument::parse(input, &painter)) {
      SetDiagnostic(diagnostic, diagnostic_capacity,
          zip && !zip->diagnostic().empty() ? zip->diagnostic().c_str() :
          "libcdr could not parse the CDR drawing stream");
      return 0;
    }
    if (pages.size() != 1) {
      SetDiagnostic(diagnostic, diagnostic_capacity,
          "This importer accepts one CDR page per document; no page was inserted");
      return 0;
    }
    const char* svg = pages[0].cstr();
    if (!svg) {
      SetDiagnostic(diagnostic, diagnostic_capacity, "libcdr returned an empty SVG page");
      return 0;
    }
    const size_t svg_length = std::strlen(svg);
    if (svg_length == 0 || svg_length > kMaxRawSvgBytes) {
      const std::string message = "Converted CDR page is " + std::to_string(svg_length) +
          " bytes, exceeding the bounded 320 MiB raw SVG profile";
      SetDiagnostic(diagnostic, diagnostic_capacity, message.c_str());
      return 0;
    }
    std::vector<uint8_t> package;
    std::string compact_diagnostic;
    if (!CompactSvgImages(std::string_view(svg, svg_length), package, compact_diagnostic)) {
      SetDiagnostic(diagnostic, diagnostic_capacity, compact_diagnostic.c_str());
      return 0;
    }
    auto* owned_package = new (std::nothrow) uint8_t[package.size()];
    if (!owned_package) {
      SetDiagnostic(diagnostic, diagnostic_capacity, "CDR compact package allocation failed");
      return 0;
    }
    std::memcpy(owned_package, package.data(), package.size());
    *output = owned_package;
    *output_size = package.size();
    return 1;
  } catch (...) {
    SetDiagnostic(diagnostic, diagnostic_capacity, "CDR parser raised an allocation or decoder exception");
    return 0;
  }
}

extern "C" __declspec(dllexport) void __cdecl PikDrawCdrFreePackage(
    uint8_t* package) noexcept {
  delete[] package;
}
