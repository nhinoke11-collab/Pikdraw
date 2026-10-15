#include "pikdraw/document/storage/native_format.hpp"
#include "pikdraw/color/preview/color_transform.hpp"
#include <cstring>
#include <sstream>

namespace pikdraw::document {

// Standard IEEE 802.3 CRC32 implementation
uint32_t NativeFormat::calculate_crc32(const uint8_t* data, size_t length) noexcept {
    static uint32_t table[256];
    static bool initialized = false;
    if (!initialized) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int j = 0; j < 8; ++j) {
                c = (c & 1) ? (0xEDB88320L ^ (c >> 1)) : (c >> 1);
            }
            table[i] = c;
        }
        initialized = true;
    }

    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < length; ++i) {
        crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFF;
}

namespace {

// Byte stream helper
class ByteWriter {
public:
    void write_u8(uint8_t v) { buf_.push_back(v); }
    void write_u32(uint32_t v) {
        uint8_t b[4];
        std::memcpy(b, &v, 4);
        buf_.insert(buf_.end(), b, b + 4);
    }
    void write_u64(uint64_t v) {
        uint8_t b[8];
        std::memcpy(b, &v, 8);
        buf_.insert(buf_.end(), b, b + 8);
    }
    void write_f64(double v) {
        uint8_t b[8];
        std::memcpy(b, &v, 8);
        buf_.insert(buf_.end(), b, b + 8);
    }
    void write_str(const std::string& s) {
        write_u32(static_cast<uint32_t>(s.size()));
        buf_.insert(buf_.end(), s.begin(), s.end());
    }
    void write_transform(const geometry::Transform2D& t) {
        write_f64(t.a);  write_f64(t.b);
        write_f64(t.c);  write_f64(t.d);
        write_f64(t.tx); write_f64(t.ty);
    }
    void write_color(const color::ColorValue& c) {
        auto rgba = color::to_preview_rgba(c);
        write_f64(rgba.r); write_f64(rgba.g); write_f64(rgba.b); write_f64(rgba.a);
    }
    const std::vector<uint8_t>& buffer() const noexcept { return buf_; }

private:
    std::vector<uint8_t> buf_;
};

class ByteReader {
public:
    ByteReader(const uint8_t* data, size_t size) : ptr_(data), end_(data + size) {}

    bool has_bytes(size_t n) const noexcept { return (ptr_ + n) <= end_; }

    bool read_u8(uint8_t& out) {
        if (!has_bytes(1)) return false;
        out = *ptr_++;
        return true;
    }
    bool read_u32(uint32_t& out) {
        if (!has_bytes(4)) return false;
        std::memcpy(&out, ptr_, 4);
        ptr_ += 4;
        return true;
    }
    bool read_u64(uint64_t& out) {
        if (!has_bytes(8)) return false;
        std::memcpy(&out, ptr_, 8);
        ptr_ += 8;
        return true;
    }
    bool read_f64(double& out) {
        if (!has_bytes(8)) return false;
        std::memcpy(&out, ptr_, 8);
        ptr_ += 8;
        return true;
    }
    bool read_str(std::string& out) {
        uint32_t len = 0;
        if (!read_u32(len) || !has_bytes(len)) return false;
        out.assign(reinterpret_cast<const char*>(ptr_), len);
        ptr_ += len;
        return true;
    }
    bool read_transform(geometry::Transform2D& t) {
        return read_f64(t.a) && read_f64(t.b) &&
               read_f64(t.c) && read_f64(t.d) &&
               read_f64(t.tx) && read_f64(t.ty);
    }
    bool read_color(color::ColorValue& c) {
        double r, g, b, a;
        if (!read_f64(r) || !read_f64(g) || !read_f64(b) || !read_f64(a)) return false;
        c = color::RGBColor{r, g, b, a};
        return true;
    }

private:
    const uint8_t* ptr_;
    const uint8_t* end_;
};

} // anonymous namespace

std::vector<uint8_t> NativeFormat::serialize(const DocumentSnapshot& snapshot) {
    ByteWriter payload;

    // Serialize Page
    const auto& page = snapshot.page();
    payload.write_f64(page.width_mm);
    payload.write_f64(page.height_mm);
    payload.write_u32(static_cast<uint32_t>(page.layers.size()));
    for (const auto& layer : page.layers) {
        payload.write_str(layer.name);
        payload.write_u8(layer.visible ? 1 : 0);
        payload.write_u8(layer.locked ? 1 : 0);
        payload.write_u32(static_cast<uint32_t>(layer.object_ids.size()));
        for (ObjectId id : layer.object_ids) {
            payload.write_u64(id);
        }
    }

    // Serialize Objects
    const auto& objects = snapshot.objects().all();
    payload.write_u32(static_cast<uint32_t>(objects.size()));
    for (const auto& [id, obj_var] : objects) {
        std::visit([&](const auto& obj) {
            using T = std::decay_t<decltype(obj)>;
            if constexpr (std::is_same_v<T, PathObject>) {
                payload.write_u8(0); // Type Path
                payload.write_u64(obj.id);
                payload.write_str(obj.name);
                payload.write_transform(obj.transform);
                payload.write_color(obj.fill);
                payload.write_color(obj.stroke);
                payload.write_f64(obj.stroke_width_mm);
                payload.write_u8(obj.has_fill ? 1 : 0);
                payload.write_u8(obj.has_stroke ? 1 : 0);
                payload.write_u8(obj.visible ? 1 : 0);
                payload.write_u8(obj.locked ? 1 : 0);

                const auto& elements = obj.path.elements();
                payload.write_u32(static_cast<uint32_t>(elements.size()));
                for (const auto& el : elements) {
                    payload.write_u8(static_cast<uint8_t>(el.verb));
                    payload.write_f64(el.pt.x); payload.write_f64(el.pt.y);
                    payload.write_f64(el.ctrl1.x); payload.write_f64(el.ctrl1.y);
                    payload.write_f64(el.ctrl2.x); payload.write_f64(el.ctrl2.y);
                    payload.write_f64(el.rx); payload.write_f64(el.ry);
                    payload.write_f64(el.rotation_deg);
                    payload.write_u8(el.large_arc ? 1 : 0);
                    payload.write_u8(el.sweep ? 1 : 0);
                }
            } else if constexpr (std::is_same_v<T, TextObject>) {
                payload.write_u8(1); // Type Text
                payload.write_u64(obj.id);
                payload.write_str(obj.name);
                payload.write_str(obj.text);
                payload.write_str(obj.font.family_name);
                payload.write_f64(obj.font.size_mm());
                payload.write_f64(obj.position.x);
                payload.write_f64(obj.position.y);
                payload.write_transform(obj.transform);
                payload.write_color(obj.fill);
                payload.write_u8(obj.visible ? 1 : 0);
                payload.write_u8(obj.locked ? 1 : 0);
            } else if constexpr (std::is_same_v<T, GroupObject>) {
                payload.write_u8(2); // Type Group
                payload.write_u64(obj.id);
                payload.write_str(obj.name);
                payload.write_transform(obj.transform);
                payload.write_u8(obj.visible ? 1 : 0);
                payload.write_u8(obj.locked ? 1 : 0);
                payload.write_u32(static_cast<uint32_t>(obj.children_ids.size()));
                for (ObjectId cid : obj.children_ids) {
                    payload.write_u64(cid);
                }
            }
        }, obj_var);
    }

    const auto& p_bytes = payload.buffer();
    uint32_t crc = calculate_crc32(p_bytes.data(), p_bytes.size());

    PikDrawHeader header;
    std::memcpy(header.magic, kPikDrawMagic, 4);
    header.version = kCurrentFormatVersion;
    header.flags = 0;
    header.epoch = snapshot.epoch();
    header.revision = snapshot.revision();
    header.payload_bytes = static_cast<uint32_t>(p_bytes.size());
    header.crc32 = crc;

    std::vector<uint8_t> out;
    out.resize(sizeof(PikDrawHeader) + p_bytes.size());
    std::memcpy(out.data(), &header, sizeof(PikDrawHeader));
    std::memcpy(out.data() + sizeof(PikDrawHeader), p_bytes.data(), p_bytes.size());
    return out;
}

std::shared_ptr<DocumentSnapshot> NativeFormat::deserialize(
    const uint8_t* data, size_t length, std::string* out_error) {
    if (length < sizeof(PikDrawHeader)) {
        if (out_error) *out_error = "File smaller than PikDraw header size";
        return nullptr;
    }

    PikDrawHeader header;
    std::memcpy(&header, data, sizeof(PikDrawHeader));

    if (std::memcmp(header.magic, kPikDrawMagic, 4) != 0) {
        if (out_error) *out_error = "Invalid PKDR magic bytes";
        return nullptr;
    }

    if (header.version != kCurrentFormatVersion) {
        if (out_error) *out_error = "Unsupported format version: " + std::to_string(header.version);
        return nullptr;
    }

    if (length != sizeof(PikDrawHeader) + header.payload_bytes) {
        if (out_error) *out_error = "Payload length mismatch";
        return nullptr;
    }

    const uint8_t* payload_ptr = data + sizeof(PikDrawHeader);
    uint32_t crc = calculate_crc32(payload_ptr, header.payload_bytes);
    if (crc != header.crc32) {
        if (out_error) *out_error = "CRC32 checksum verification failed";
        return nullptr;
    }

    ByteReader reader(payload_ptr, header.payload_bytes);
    Page page;
    if (!reader.read_f64(page.width_mm) || !reader.read_f64(page.height_mm)) {
        if (out_error) *out_error = "Failed to read page dimensions";
        return nullptr;
    }

    uint32_t layer_count = 0;
    if (!reader.read_u32(layer_count)) return nullptr;
    page.layers.resize(layer_count);
    for (size_t i = 0; i < layer_count; ++i) {
        auto& layer = page.layers[i];
        uint8_t vis, lock;
        uint32_t id_count = 0;
        if (!reader.read_str(layer.name) || !reader.read_u8(vis) ||
            !reader.read_u8(lock) || !reader.read_u32(id_count)) {
            if (out_error) *out_error = "Corrupted layer data";
            return nullptr;
        }
        layer.visible = (vis != 0);
        layer.locked = (lock != 0);
        layer.object_ids.resize(id_count);
        for (size_t j = 0; j < id_count; ++j) {
            if (!reader.read_u64(layer.object_ids[j])) return nullptr;
        }
    }

    uint32_t obj_count = 0;
    if (!reader.read_u32(obj_count)) return nullptr;
    PersistentObjectMap obj_map;

    for (size_t i = 0; i < obj_count; ++i) {
        uint8_t type = 0;
        if (!reader.read_u8(type)) return nullptr;

        if (type == 0) { // PathObject
            PathObject path_obj;
            uint8_t hf, hs, vis, lk;
            if (!reader.read_u64(path_obj.id) || !reader.read_str(path_obj.name) ||
                !reader.read_transform(path_obj.transform) ||
                !reader.read_color(path_obj.fill) || !reader.read_color(path_obj.stroke) ||
                !reader.read_f64(path_obj.stroke_width_mm) ||
                !reader.read_u8(hf) || !reader.read_u8(hs) ||
                !reader.read_u8(vis) || !reader.read_u8(lk)) {
                return nullptr;
            }
            path_obj.has_fill = (hf != 0);
            path_obj.has_stroke = (hs != 0);
            path_obj.visible = (vis != 0);
            path_obj.locked = (lk != 0);

            uint32_t el_count = 0;
            if (!reader.read_u32(el_count)) return nullptr;
            for (size_t e = 0; e < el_count; ++e) {
                uint8_t vt = 0;
                geometry::Point2D pt, c1, c2;
                double rx, ry, rot_deg;
                uint8_t la, sw;
                if (!reader.read_u8(vt) ||
                    !reader.read_f64(pt.x) || !reader.read_f64(pt.y) ||
                    !reader.read_f64(c1.x) || !reader.read_f64(c1.y) ||
                    !reader.read_f64(c2.x) || !reader.read_f64(c2.y) ||
                    !reader.read_f64(rx) || !reader.read_f64(ry) ||
                    !reader.read_f64(rot_deg) ||
                    !reader.read_u8(la) || !reader.read_u8(sw)) {
                    return nullptr;
                }
                auto verb = static_cast<geometry::PathVerb>(vt);
                if (verb == geometry::PathVerb::MoveTo) path_obj.path.move_to(pt);
                else if (verb == geometry::PathVerb::LineTo) path_obj.path.line_to(pt);
                else if (verb == geometry::PathVerb::QuadTo) path_obj.path.quad_to(c1.x, c1.y, pt.x, pt.y);
                else if (verb == geometry::PathVerb::CubicTo) path_obj.path.cubic_to(c1.x, c1.y, c2.x, c2.y, pt.x, pt.y);
                else if (verb == geometry::PathVerb::ArcTo) path_obj.path.arc_to(rx, ry, rot_deg, la != 0, sw != 0, pt.x, pt.y);
                else if (verb == geometry::PathVerb::Close) path_obj.path.close();
            }
            obj_map = obj_map.set(path_obj.id, std::move(path_obj));
        } else if (type == 1) { // TextObject
            TextObject text_obj;
            std::string font_family;
            double font_size_mm;
            uint8_t vis, lk;
            if (!reader.read_u64(text_obj.id) || !reader.read_str(text_obj.name) ||
                !reader.read_str(text_obj.text) || !reader.read_str(font_family) ||
                !reader.read_f64(font_size_mm) ||
                !reader.read_f64(text_obj.position.x) || !reader.read_f64(text_obj.position.y) ||
                !reader.read_transform(text_obj.transform) || !reader.read_color(text_obj.fill) ||
                !reader.read_u8(vis) || !reader.read_u8(lk)) {
                return nullptr;
            }
            text_obj.font.family_name = font_family;
            text_obj.font.size_pt = font_size_mm / (25.4 / 72.0);
            text_obj.visible = (vis != 0);
            text_obj.locked = (lk != 0);
            obj_map = obj_map.set(text_obj.id, std::move(text_obj));
        } else if (type == 2) { // GroupObject
            GroupObject grp;
            uint8_t vis, lk;
            uint32_t cid_count = 0;
            if (!reader.read_u64(grp.id) || !reader.read_str(grp.name) ||
                !reader.read_transform(grp.transform) ||
                !reader.read_u8(vis) || !reader.read_u8(lk) ||
                !reader.read_u32(cid_count)) {
                return nullptr;
            }
            grp.visible = (vis != 0);
            grp.locked = (lk != 0);
            grp.children_ids.resize(cid_count);
            for (size_t c = 0; c < cid_count; ++c) {
                if (!reader.read_u64(grp.children_ids[c])) return nullptr;
            }
            obj_map = obj_map.set(grp.id, std::move(grp));
        }
    }

    return std::make_shared<DocumentSnapshot>(header.epoch, header.revision, std::move(page), std::move(obj_map));
}

} // namespace pikdraw::document
