#include "pikdraw/document/persistence.hpp"
#include <fstream>
#include <cstring>
#include <iostream>
#include <mutex>
#include <limits>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace pikdraw::document {

namespace {

// IEEE CRC32 polynomial; covered by an independent zlib check in leader audit.
constexpr uint32_t crc32_for_byte(uint32_t r) {
    for (int bit=0;bit<8;++bit) r=(r>>1)^((r&1)?0xEDB88320u:0u);
    return r;
}

uint32_t compute_crc32(const uint8_t* data, size_t length) {
    uint32_t crc = 0xFFFFFFFFL;
    for (size_t i = 0; i < length; ++i) {
        crc = (crc >> 8) ^ crc32_for_byte((crc & 0xFF) ^ data[i]);
    }
    return crc ^ 0xFFFFFFFFL;
}

constexpr uint32_t PIK_MAGIC = 0x444B4950; // "PIKD"
constexpr uint32_t FORMAT_VERSION = 1;
constexpr size_t MAX_PAYLOAD_BYTES = 16 * 1024 * 1024;
constexpr size_t MAX_OBJECTS = 100000;

void write_u32(std::vector<uint8_t>& buf, uint32_t v) {
    buf.push_back(static_cast<uint8_t>(v & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

void write_u64(std::vector<uint8_t>& buf, uint64_t v) {
    for (int i = 0; i < 8; ++i) {
        buf.push_back(static_cast<uint8_t>((v >> (i * 8)) & 0xFF));
    }
}

void write_f64(std::vector<uint8_t>& buf, double v) {
    uint64_t raw;
    std::memcpy(&raw, &v, sizeof(raw));
    write_u64(buf, raw);
}

void write_str(std::vector<uint8_t>& buf, const std::string& str) {
    write_u32(buf, static_cast<uint32_t>(str.size()));
    buf.insert(buf.end(), str.begin(), str.end());
}

uint8_t read_u8(const uint8_t*& ptr, const uint8_t* end) {
    if (ptr==end) throw std::runtime_error("Truncated byte");
    return *ptr++;
}

uint32_t read_u32(const uint8_t*& ptr, const uint8_t* end) {
    if (static_cast<size_t>(end-ptr) < 4) throw std::runtime_error("Truncated payload");
    uint32_t v = static_cast<uint32_t>(ptr[0]) |
                 (static_cast<uint32_t>(ptr[1]) << 8) |
                 (static_cast<uint32_t>(ptr[2]) << 16) |
                 (static_cast<uint32_t>(ptr[3]) << 24);
    ptr += 4;
    return v;
}

uint64_t read_u64(const uint8_t*& ptr, const uint8_t* end) {
    if (static_cast<size_t>(end-ptr) < 8) throw std::runtime_error("Truncated payload");
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i) {
        v |= (static_cast<uint64_t>(ptr[i]) << (i * 8));
    }
    ptr += 8;
    return v;
}

double read_f64(const uint8_t*& ptr, const uint8_t* end) {
    uint64_t raw = read_u64(ptr, end);
    double v;
    std::memcpy(&v, &raw, sizeof(v));
    return v;
}

std::string read_str(const uint8_t*& ptr, const uint8_t* end) {
    uint32_t len = read_u32(ptr, end);
    if (len > static_cast<size_t>(end-ptr)) throw std::runtime_error("Truncated string");
    std::string s(reinterpret_cast<const char*>(ptr), len);
    ptr += len;
    return s;
}

} // namespace

std::vector<uint8_t> PersistenceManager::serialize_to_buffer(const DocumentSnapshot& snapshot) {
    std::vector<uint8_t> payload;
    // Collect all objects
    std::vector<std::shared_ptr<const DocumentObject>> all_objs;
    for (const auto& [cid, chunk] : snapshot.chunks()) {
        if (chunk) {
            for (const auto& obj : chunk->objects) {
                if (obj) all_objs.push_back(obj);
            }
        }
    }

    write_str(payload, snapshot.doc_id());
    write_u64(payload, snapshot.epoch());
    write_u64(payload, snapshot.revision());
    write_u32(payload, static_cast<uint32_t>(all_objs.size()));

    for (const auto& obj : all_objs) {
        payload.push_back(static_cast<uint8_t>(obj->type()));
        write_u64(payload, obj->id);
        write_str(payload, obj->name);
        // Transform
        write_f64(payload, obj->transform.a);
        write_f64(payload, obj->transform.b);
        write_f64(payload, obj->transform.c);
        write_f64(payload, obj->transform.d);
        write_f64(payload, obj->transform.tx);
        write_f64(payload, obj->transform.ty);
        // Stroke color
        payload.push_back(static_cast<uint8_t>(obj->stroke_color.model));
        write_f64(payload, obj->stroke_color.c0);
        write_f64(payload, obj->stroke_color.c1);
        write_f64(payload, obj->stroke_color.c2);
        write_f64(payload, obj->stroke_color.c3);
        write_str(payload, obj->stroke_color.spot_name);
        write_f64(payload, obj->stroke_color.spot_tint);
        write_str(payload, obj->stroke_color.icc_profile_name);
        payload.push_back(obj->stroke_color.overprint ? 1 : 0);
        // Fill color
        payload.push_back(static_cast<uint8_t>(obj->fill_color.model));
        write_f64(payload, obj->fill_color.c0);
        write_f64(payload, obj->fill_color.c1);
        write_f64(payload, obj->fill_color.c2);
        write_f64(payload, obj->fill_color.c3);
        write_str(payload, obj->fill_color.spot_name);
        write_f64(payload, obj->fill_color.spot_tint);
        write_str(payload, obj->fill_color.icc_profile_name);
        payload.push_back(obj->fill_color.overprint ? 1 : 0);
        write_f64(payload, obj->stroke_width_mm);
        payload.push_back(obj->visible ? 1 : 0);
        payload.push_back(obj->locked ? 1 : 0);

        if (obj->type() == ObjectType::Path) {
            const auto* p = static_cast<const PathObject*>(obj.get());
            write_u32(payload, static_cast<uint32_t>(p->path.segments.size()));
            for (const auto& s : p->path.segments) {
                payload.push_back(static_cast<uint8_t>(s.type));
                write_f64(payload, s.p0.x); write_f64(payload, s.p0.y);
                write_f64(payload, s.p1.x); write_f64(payload, s.p1.y);
                write_f64(payload, s.p2.x); write_f64(payload, s.p2.y);
                write_f64(payload, s.p3.x); write_f64(payload, s.p3.y);
            }
        } else if (obj->type() == ObjectType::Text) {
            const auto* t = static_cast<const TextObject*>(obj.get());
            write_str(payload, t->utf8_text);
            write_str(payload, t->font_family);
            write_f64(payload, t->font_size_pt);
            payload.push_back(t->is_bold ? 1 : 0);
            payload.push_back(t->is_italic ? 1 : 0);
        } else if (obj->type()==ObjectType::Group) {
            const auto& group=static_cast<const GroupObject&>(*obj);
            if (group.child_ids.size()>MAX_OBJECTS) throw std::runtime_error("Group too large");
            write_u32(payload,static_cast<uint32_t>(group.child_ids.size()));
            for (auto id:group.child_ids) write_u64(payload,id);
        } else throw std::runtime_error("Unsupported object type");
    }

    if (payload.size()>MAX_PAYLOAD_BYTES || all_objs.size()>MAX_OBJECTS) throw std::runtime_error("Native save budget exceeded");
    const uint32_t crc = compute_crc32(payload.data(), payload.size());
    std::vector<uint8_t> file_bytes;
    write_u32(file_bytes, PIK_MAGIC);
    write_u32(file_bytes, FORMAT_VERSION);
    write_u32(file_bytes, crc);
    write_u64(file_bytes, static_cast<uint64_t>(payload.size()));
    file_bytes.insert(file_bytes.end(), payload.begin(), payload.end());
    return file_bytes;
}

std::shared_ptr<const DocumentSnapshot> PersistenceManager::deserialize_from_buffer(const std::vector<uint8_t>& buffer) {
    if (buffer.size() < 24) {
        throw std::runtime_error("File too small for header");
    }
    const uint8_t* ptr = buffer.data();
    const uint8_t* end = buffer.data() + buffer.size();

    const uint32_t magic = read_u32(ptr, end);
    if (magic != PIK_MAGIC) throw std::runtime_error("Invalid magic identifier");
    const uint32_t version = read_u32(ptr, end);
    if (version != FORMAT_VERSION) throw std::runtime_error("Unsupported version");
    const uint32_t expected_crc = read_u32(ptr, end);
    const uint64_t payload_len = read_u64(ptr, end);

    if (payload_len > MAX_PAYLOAD_BYTES) {
        throw std::runtime_error("Payload exceeds max allowed size (16MB)");
    }
    if (static_cast<size_t>(end - ptr) != payload_len) {
        throw std::runtime_error("Payload size mismatch");
    }
    const uint32_t actual_crc = compute_crc32(ptr, payload_len);
    if (actual_crc != expected_crc) {
        throw std::runtime_error("CRC32 checksum mismatch (file corrupted)");
    }

    const std::string doc_id = read_str(ptr, end);
    const Epoch epoch = read_u64(ptr, end);
    const Revision revision = read_u64(ptr, end);
    const uint32_t obj_count = read_u32(ptr, end);
    if (obj_count > MAX_OBJECTS) {
        throw std::runtime_error("Object count exceeds max limit");
    }

    ChunkMap chunks;
    std::unordered_map<ObjectId, pikdraw::geometry::Rect2D> spatial_index;

    for (uint32_t i = 0; i < obj_count; ++i) {
        if (ptr >= end) throw std::runtime_error("Premature end of objects");
        const ObjectType otype = static_cast<ObjectType>(read_u8(ptr,end));
        const ObjectId oid = read_u64(ptr, end);
        const std::string oname = read_str(ptr, end);
        const double ma = read_f64(ptr, end);
        const double mb = read_f64(ptr, end);
        const double mc = read_f64(ptr, end);
        const double md = read_f64(ptr, end);
        const double mtx = read_f64(ptr, end);
        const double mty = read_f64(ptr, end);

        auto read_col = [&]() {
            ColorData c;
            c.model = static_cast<ColorModel>(read_u8(ptr,end));
            if (c.model>ColorModel::Spot) throw std::runtime_error("Unknown color model");
            c.c0 = static_cast<float>(read_f64(ptr, end));
            c.c1 = static_cast<float>(read_f64(ptr, end));
            c.c2 = static_cast<float>(read_f64(ptr, end));
            c.c3 = static_cast<float>(read_f64(ptr, end));
            c.spot_name = read_str(ptr, end);
            c.spot_tint = static_cast<float>(read_f64(ptr, end));
            c.icc_profile_name = read_str(ptr, end);
            c.overprint = (read_u8(ptr,end) != 0);
            return c;
        };
        ColorData stroke = read_col();
        ColorData fill = read_col();
        double stroke_w = read_f64(ptr, end);
        bool visible = (read_u8(ptr,end) != 0);
        bool locked = (read_u8(ptr,end) != 0);

        std::shared_ptr<DocumentObject> obj;
        if (otype == ObjectType::Path) {
            auto po = std::make_shared<PathObject>();
            uint32_t seg_count = read_u32(ptr, end);
            if (seg_count > static_cast<size_t>(end-ptr)/65) throw std::runtime_error("Invalid segment count");
            for (uint32_t s = 0; s < seg_count; ++s) {
                pikdraw::geometry::PathSegment ps;
                ps.type = static_cast<pikdraw::geometry::SegmentType>(read_u8(ptr,end));
                if (ps.type>pikdraw::geometry::SegmentType::Close) throw std::runtime_error("Unknown segment type");
                ps.p0.x = read_f64(ptr, end); ps.p0.y = read_f64(ptr, end);
                ps.p1.x = read_f64(ptr, end); ps.p1.y = read_f64(ptr, end);
                ps.p2.x = read_f64(ptr, end); ps.p2.y = read_f64(ptr, end);
                ps.p3.x = read_f64(ptr, end); ps.p3.y = read_f64(ptr, end);
                po->path.segments.push_back(ps);
            }
            obj = po;
        } else if (otype == ObjectType::Text) {
            auto to = std::make_shared<TextObject>();
            to->utf8_text = read_str(ptr, end);
            to->font_family = read_str(ptr, end);
            to->font_size_pt = read_f64(ptr, end);
            to->is_bold = (read_u8(ptr,end) != 0);
            to->is_italic = (read_u8(ptr,end) != 0);
            obj = to;
        } else if (otype==ObjectType::Group) {
            auto group=std::make_shared<GroupObject>();
            const auto count=read_u32(ptr,end);
            if (count>MAX_OBJECTS || count>static_cast<size_t>(end-ptr)/8) throw std::runtime_error("Invalid group count");
            for (uint32_t j=0;j<count;++j) group->child_ids.push_back(read_u64(ptr,end));
            obj=group;
        } else throw std::runtime_error("Unknown object type");

        obj->id = oid;
        obj->name = oname;
        obj->transform = pikdraw::geometry::Transform2D(ma, mb, mc, md, mtx, mty);
        obj->stroke_color = stroke;
        obj->fill_color = fill;
        obj->stroke_width_mm = stroke_w;
        obj->visible = visible;
        obj->locked = locked;
        validate_source_object(*obj);

        if (!oid || spatial_index.contains(oid) || oid/OBJECTS_PER_CHUNK>std::numeric_limits<ChunkId>::max() ||
            !obj->transform.is_finite() || !obj->bounds().is_finite()) throw std::runtime_error("Invalid object ID or geometry");
        const ChunkId cid = static_cast<ChunkId>(oid / OBJECTS_PER_CHUNK);
        auto& chunk_ptr = chunks[cid];
        if (!chunk_ptr) {
            chunk_ptr = std::make_shared<ObjectChunk>();
        }
        const_cast<ObjectChunk*>(chunk_ptr.get())->objects.push_back(obj);
        spatial_index[oid] = obj->bounds();
    }

    if (ptr!=end) throw std::runtime_error("Trailing native payload");
    return std::make_shared<const DocumentSnapshot>(
        doc_id, epoch, revision, std::move(chunks), std::move(spatial_index)
    );
}

SaveResult PersistenceManager::save_snapshot(
    const std::shared_ptr<const DocumentSnapshot>& snapshot,
    const std::filesystem::path& target_path,
    FaultInjectionPoint fault
) {
    SaveResult res;
    if (!snapshot) { res.error_message="Null snapshot"; return res; }
    res.epoch = snapshot->epoch();
    res.revision = snapshot->revision();

    if (fault == FaultInjectionPoint::BeforeWrite) {
        res.success = false;
        res.error_message = "Injected fault before write";
        return res;
    }

    // Serialize saves in this prototype; unique sibling temp names preserve Unicode paths.
    static std::mutex save_mutex;
    std::lock_guard<std::mutex> save_lock(save_mutex);
    static std::atomic<uint64_t> serial{0};
    auto temp_path=target_path;
    temp_path += std::filesystem::path(".tmp."+std::to_string(++serial)+".pikdraw").native();
    try {
        std::vector<uint8_t> buffer = serialize_to_buffer(*snapshot);
        if (fault == FaultInjectionPoint::DuringWrite) {
            buffer.resize(buffer.size() / 2); // Truncate payload
        }

        {
            std::ofstream out(temp_path, std::ios::binary);
            if (!out.is_open()) {
                res.success = false;
                res.error_message = "Failed to open temporary file for writing";
                return res;
            }
            out.write(reinterpret_cast<const char*>(buffer.data()), buffer.size());
            if (fault == FaultInjectionPoint::DuringWrite) {
                out.close();
                std::filesystem::remove(temp_path);
                res.success = false;
                res.error_message = "Injected fault during write";
                return res;
            }
            if (fault == FaultInjectionPoint::BeforeFlush) {
                out.close();
                std::filesystem::remove(temp_path);
                res.success = false;
                res.error_message = "Injected fault before flush";
                return res;
            }
            out.flush();
            if (!out) throw std::runtime_error("Native write/flush failed");
        }
#ifdef _WIN32
        HANDLE flush_handle=CreateFileW(temp_path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
        if (flush_handle==INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot open native temp for OS flush");
        const BOOL flushed=FlushFileBuffers(flush_handle);
        CloseHandle(flush_handle);
        if (!flushed) throw std::runtime_error("OS file flush failed");
#endif

        if (fault == FaultInjectionPoint::BeforeReplace) {
            std::filesystem::remove(temp_path);
            res.success = false;
            res.error_message = "Injected fault before replace";
            return res;
        }

        // Safe atomic replacement (never delete target before replace)
#ifdef _WIN32
        std::wstring wtarget = target_path.wstring();
        std::wstring wtemp = std::filesystem::path(temp_path).wstring();
        if (std::filesystem::exists(target_path)) {
            if (!ReplaceFileW(wtarget.c_str(), wtemp.c_str(), nullptr, REPLACEFILE_IGNORE_MERGE_ERRORS, nullptr, nullptr)) {
                // Fallback to MoveFileExW
                if (!MoveFileExW(wtemp.c_str(), wtarget.c_str(), MOVEFILE_REPLACE_EXISTING)) {
                    std::filesystem::remove(temp_path);
                    res.success = false;
                    res.error_message = "Windows ReplaceFileW failed";
                    return res;
                }
            }
        } else {
            if (!MoveFileExW(wtemp.c_str(), wtarget.c_str(), 0)) {
                std::filesystem::remove(temp_path);
                res.success = false;
                res.error_message = "Windows MoveFileExW failed";
                return res;
            }
        }
#else
        std::filesystem::rename(temp_path, target_path);
#endif

        if (fault == FaultInjectionPoint::AfterReplace) {
            res.success = false;
            res.error_message = "Injected fault after replace";
            return res;
        }

        res.success = true;
        return res;
    } catch (const std::exception& e) {
        std::error_code ec;
        std::filesystem::remove(temp_path, ec);
        res.success = false;
        res.error_message = e.what();
        return res;
    }
}

LoadResult PersistenceManager::load_document(const std::filesystem::path& source_path) {
    LoadResult res;
    try {
        std::ifstream in(source_path, std::ios::binary | std::ios::ate);
        if (!in.is_open()) {
            res.success = false;
            res.error_message = "Failed to open file";
            return res;
        }
        const auto size = in.tellg();
        in.seekg(0, std::ios::beg);
        if (size < 0 || size > 64 * 1024 * 1024) throw std::runtime_error("Native file exceeds 64 MiB spike limit");
        std::vector<uint8_t> buffer(static_cast<size_t>(size));
        if (!in.read(reinterpret_cast<char*>(buffer.data()), size)) {
            res.success = false;
            res.error_message = "Failed to read file contents";
            return res;
        }

        auto snapshot = deserialize_from_buffer(buffer);
        auto doc = std::make_unique<Document>(snapshot->doc_id());
        doc->restore_snapshot(*snapshot);
        res.success = true;
        res.document = std::move(doc);
        return res;
    } catch (const std::exception& e) {
        res.success = false;
        res.error_message = e.what();
        return res;
    }
}

} // namespace pikdraw::document
