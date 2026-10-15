#include "pikdraw/interop/ipc_dto.hpp"
#include <cstring>
#include <sstream>
#include <stdexcept>
#include "pikdraw/interop/payload_validator.hpp"

namespace pikdraw::interop {

namespace {
    void EnsureAppend(const std::vector<uint8_t>& buf, size_t bytes) {
        if (buf.size() > IPC_MAX_PAYLOAD_BYTES || bytes > IPC_MAX_PAYLOAD_BYTES - buf.size())
            throw std::length_error("IPC exceeds 16 MiB budget");
    }
    void WriteUint16(std::vector<uint8_t>& buf, uint16_t v) {
        EnsureAppend(buf, 2);
        buf.push_back(static_cast<uint8_t>(v & 0xFF));
        buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    }
    void WriteUint32(std::vector<uint8_t>& buf, uint32_t v) {
        EnsureAppend(buf, 4);
        buf.push_back(static_cast<uint8_t>(v & 0xFF));
        buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
        buf.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
        buf.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
    }
    void WriteUint64(std::vector<uint8_t>& buf, uint64_t v) {
        EnsureAppend(buf, 8);
        for (int i = 0; i < 8; ++i) {
            buf.push_back(static_cast<uint8_t>((v >> (i * 8)) & 0xFF));
        }
    }
    void WriteDouble(std::vector<uint8_t>& buf, double v) {
        uint64_t u;
        std::memcpy(&u, &v, sizeof(double));
        WriteUint64(buf, u);
    }
    void WriteString(std::vector<uint8_t>& buf, const std::string& str) {
        EnsureAppend(buf, 4 + str.size());
        WriteUint32(buf, static_cast<uint32_t>(str.size()));
        buf.insert(buf.end(), str.begin(), str.end());
    }

    bool ReadUint16(std::span<const uint8_t>& s, uint16_t& out) {
        if (s.size() < 2) return false;
        out = static_cast<uint16_t>(s[0]) | (static_cast<uint16_t>(s[1]) << 8);
        s = s.subspan(2);
        return true;
    }
    bool ReadUint32(std::span<const uint8_t>& s, uint32_t& out) {
        if (s.size() < 4) return false;
        out = static_cast<uint32_t>(s[0]) |
              (static_cast<uint32_t>(s[1]) << 8) |
              (static_cast<uint32_t>(s[2]) << 16) |
              (static_cast<uint32_t>(s[3]) << 24);
        s = s.subspan(4);
        return true;
    }
    bool ReadUint64(std::span<const uint8_t>& s, uint64_t& out) {
        if (s.size() < 8) return false;
        out = 0;
        for (int i = 0; i < 8; ++i) {
            out |= (static_cast<uint64_t>(s[i]) << (i * 8));
        }
        s = s.subspan(8);
        return true;
    }
    bool ReadDouble(std::span<const uint8_t>& s, double& out) {
        uint64_t u;
        if (!ReadUint64(s, u)) return false;
        std::memcpy(&out, &u, sizeof(double));
        return true;
    }
    bool ReadString(std::span<const uint8_t>& s, std::string& out) {
        uint32_t len = 0;
        if (!ReadUint32(s, len)) return false;
        if (s.size() < len) return false;
        out.assign(reinterpret_cast<const char*>(s.data()), len);
        s = s.subspan(len);
        return true;
    }
} // namespace

bool IsV2MessageType(uint16_t message_type) noexcept {
    switch (static_cast<MessageType>(message_type)) {
        case MessageType::V2NegotiateRequest:
        case MessageType::V2NegotiateResponse:
        case MessageType::V2ImportBegin:
        case MessageType::V2ProposalChunk:
        case MessageType::V2ImportEnd:
        case MessageType::V2ErrorResponse:
            return true;
        default:
            return false;
    }
}

bool ValidateV2FrameHeader(const IpcHeader& header) noexcept {
    return header.magic == IPC_MAGIC &&
           header.wire_version == IPC_WIRE_VERSION_V2 &&
           IsV2MessageType(header.message_type) &&
           header.payload_bytes <= IPC_V2_MAX_FRAME_BYTES;
}

std::vector<uint8_t> SerializeV2Frame(
    MessageType message_type,
    uint32_t sequence_id,
    std::span<const uint8_t> payload) {
    if (!IsV2MessageType(static_cast<uint16_t>(message_type)) ||
        payload.size() > IPC_V2_MAX_FRAME_BYTES) {
        throw std::invalid_argument("Invalid v2 message type or payload size");
    }

    IpcHeader header{};
    header.wire_version = IPC_WIRE_VERSION_V2;
    header.message_type = static_cast<uint16_t>(message_type);
    header.sequence_id = sequence_id;
    header.payload_bytes = static_cast<uint32_t>(payload.size());

    std::vector<uint8_t> packet(sizeof(IpcHeader) + payload.size());
    std::memcpy(packet.data(), &header, sizeof(header));
    if (!payload.empty()) {
        std::memcpy(packet.data() + sizeof(header), payload.data(), payload.size());
    }
    return packet;
}

std::optional<IpcV2Frame> DeserializeV2Frame(std::span<const uint8_t> packet) {
    if (packet.size() < sizeof(IpcHeader)) return std::nullopt;

    IpcV2Frame frame{};
    std::memcpy(&frame.header, packet.data(), sizeof(frame.header));
    if (!ValidateV2FrameHeader(frame.header) ||
        packet.size() != sizeof(IpcHeader) + frame.header.payload_bytes) {
        return std::nullopt;
    }

    try {
        frame.payload.assign(packet.begin() + sizeof(IpcHeader), packet.end());
    } catch (const std::bad_alloc&) {
        return std::nullopt;
    }
    return frame;
}

std::vector<uint8_t> SerializeImportRequest(const ImportPayloadRequest& req, uint32_t sequence_id) {
    std::vector<uint8_t> payload;
    WriteString(payload, req.job.document_id);
    WriteUint64(payload, req.job.epoch);
    WriteUint64(payload, req.job.base_revision);
    WriteUint64(payload, req.job.generation);
    WriteString(payload, req.source_format);
    WriteUint32(payload, static_cast<uint32_t>(req.raw_input_bytes.size()));
    EnsureAppend(payload, req.raw_input_bytes.size());
    payload.insert(payload.end(), req.raw_input_bytes.begin(), req.raw_input_bytes.end());

    IpcHeader hdr{};
    hdr.magic = IPC_MAGIC;
    hdr.wire_version = IPC_WIRE_VERSION;
    hdr.message_type = static_cast<uint16_t>(MessageType::ImportRequest);
    hdr.sequence_id = sequence_id;
    EnsureAppend(payload, 0);
    hdr.payload_bytes = static_cast<uint32_t>(payload.size());

    std::vector<uint8_t> packet(sizeof(IpcHeader));
    std::memcpy(packet.data(), &hdr, sizeof(IpcHeader));
    packet.insert(packet.end(), payload.begin(), payload.end());
    return packet;
}

std::optional<ImportPayloadRequest> DeserializeImportRequest(std::span<const uint8_t> data) {
    if (data.size() < sizeof(IpcHeader)) return std::nullopt;
    IpcHeader hdr{};
    std::memcpy(&hdr, data.data(), sizeof(IpcHeader));
    if (hdr.magic != IPC_MAGIC || hdr.wire_version != IPC_WIRE_VERSION) return std::nullopt;
    if (hdr.message_type != static_cast<uint16_t>(MessageType::ImportRequest)) return std::nullopt;
    if (hdr.payload_bytes > IPC_MAX_PAYLOAD_BYTES || data.size() != sizeof(IpcHeader) + hdr.payload_bytes) return std::nullopt;

    std::span<const uint8_t> s = data.subspan(sizeof(IpcHeader), hdr.payload_bytes);
    ImportPayloadRequest req{};
    if (!ReadString(s, req.job.document_id)) return std::nullopt;
    if (!ReadUint64(s, req.job.epoch)) return std::nullopt;
    if (!ReadUint64(s, req.job.base_revision)) return std::nullopt;
    if (!ReadUint64(s, req.job.generation)) return std::nullopt;
    if (!ReadString(s, req.source_format)) return std::nullopt;
    uint32_t byte_len = 0;
    if (!ReadUint32(s, byte_len)) return std::nullopt;
    if (s.size() != byte_len) return std::nullopt;
    req.raw_input_bytes.assign(s.begin(), s.begin() + byte_len);
    s=s.subspan(byte_len);
    if (!s.empty()) return std::nullopt;
    return req;
}

std::vector<uint8_t> SerializeImportResponse(const ImportPayloadResponse& resp, uint32_t sequence_id) {
    std::vector<uint8_t> payload;
    WriteString(payload, resp.job.document_id);
    WriteUint64(payload, resp.job.epoch);
    WriteUint64(payload, resp.job.base_revision);
    WriteUint64(payload, resp.job.generation);
    payload.push_back(resp.success ? 1 : 0);
    WriteString(payload, resp.error_message);

    if (!PayloadValidator::ValidateGraphicPayload(resp.objects).IsOk()) throw std::invalid_argument("Invalid objects");
    WriteUint32(payload, static_cast<uint32_t>(resp.objects.size()));
    for (const auto& obj : resp.objects) {
        WriteString(payload, obj.id);
        WriteDouble(payload, obj.transform.a);
        WriteDouble(payload, obj.transform.b);
        WriteDouble(payload, obj.transform.c);
        WriteDouble(payload, obj.transform.d);
        WriteDouble(payload, obj.transform.tx);
        WriteDouble(payload, obj.transform.ty);
        WriteDouble(payload, obj.stroke_width_mm);
        WriteUint32(payload, obj.depth);
        for (double channel : {obj.stroke_color.r,obj.stroke_color.g,obj.stroke_color.b,obj.stroke_color.a,
                               obj.fill_color.r,obj.fill_color.g,obj.fill_color.b,obj.fill_color.a}) WriteDouble(payload,channel);

        WriteUint32(payload, static_cast<uint32_t>(obj.segments.size()));
        for (const auto& seg : obj.segments) {
            payload.push_back(static_cast<uint8_t>(seg.type));
            WriteDouble(payload, seg.p1.x);
            WriteDouble(payload, seg.p1.y);
            WriteDouble(payload, seg.p2.x);
            WriteDouble(payload, seg.p2.y);
            WriteDouble(payload, seg.p3.x);
            WriteDouble(payload, seg.p3.y);
        }
    }

    WriteUint32(payload, static_cast<uint32_t>(resp.loss_reports.size()));
    for (const auto& report : resp.loss_reports) {
        WriteString(payload, report);
    }

    IpcHeader hdr{};
    hdr.magic = IPC_MAGIC;
    hdr.wire_version = IPC_WIRE_VERSION;
    hdr.message_type = static_cast<uint16_t>(MessageType::ImportResponse);
    hdr.sequence_id = sequence_id;
    EnsureAppend(payload, 0);
    hdr.payload_bytes = static_cast<uint32_t>(payload.size());

    std::vector<uint8_t> packet(sizeof(IpcHeader));
    std::memcpy(packet.data(), &hdr, sizeof(IpcHeader));
    packet.insert(packet.end(), payload.begin(), payload.end());
    return packet;
}

std::optional<ImportPayloadResponse> DeserializeImportResponse(std::span<const uint8_t> data) {
    if (data.size() < sizeof(IpcHeader)) return std::nullopt;
    IpcHeader hdr{};
    std::memcpy(&hdr, data.data(), sizeof(IpcHeader));
    if (hdr.magic != IPC_MAGIC || hdr.wire_version != IPC_WIRE_VERSION) return std::nullopt;
    if (hdr.message_type != static_cast<uint16_t>(MessageType::ImportResponse)) return std::nullopt;
    if (hdr.payload_bytes > IPC_MAX_PAYLOAD_BYTES || data.size() != sizeof(IpcHeader) + hdr.payload_bytes) return std::nullopt;

    std::span<const uint8_t> s = data.subspan(sizeof(IpcHeader), hdr.payload_bytes);
    ImportPayloadResponse resp{};
    if (!ReadString(s, resp.job.document_id)) return std::nullopt;
    if (!ReadUint64(s, resp.job.epoch)) return std::nullopt;
    if (!ReadUint64(s, resp.job.base_revision)) return std::nullopt;
    if (!ReadUint64(s, resp.job.generation)) return std::nullopt;
    if (s.empty() || s[0] > 1) return std::nullopt;
    resp.success = (s[0] != 0);
    s = s.subspan(1);
    if (!ReadString(s, resp.error_message)) return std::nullopt;

    uint32_t obj_count = 0;
    if (!ReadUint32(s, obj_count)) return std::nullopt;
    if (obj_count > IPC_MAX_OBJECTS) return std::nullopt;
    resp.objects.reserve(obj_count);

    for (uint32_t i = 0; i < obj_count; ++i) {
        VectorGraphicObject obj{};
        if (!ReadString(s, obj.id)) return std::nullopt;
        if (!ReadDouble(s, obj.transform.a)) return std::nullopt;
        if (!ReadDouble(s, obj.transform.b)) return std::nullopt;
        if (!ReadDouble(s, obj.transform.c)) return std::nullopt;
        if (!ReadDouble(s, obj.transform.d)) return std::nullopt;
        if (!ReadDouble(s, obj.transform.tx)) return std::nullopt;
        if (!ReadDouble(s, obj.transform.ty)) return std::nullopt;
        if (!ReadDouble(s, obj.stroke_width_mm)) return std::nullopt;
        if (!ReadUint32(s, obj.depth)) return std::nullopt;
        for (double* channel : {&obj.stroke_color.r,&obj.stroke_color.g,&obj.stroke_color.b,&obj.stroke_color.a,
                                &obj.fill_color.r,&obj.fill_color.g,&obj.fill_color.b,&obj.fill_color.a})
            if (!ReadDouble(s,*channel)) return std::nullopt;

        uint32_t seg_count = 0;
        if (!ReadUint32(s, seg_count)) return std::nullopt;
        if (seg_count > s.size()/49) return std::nullopt;
        obj.segments.reserve(seg_count);
        for (uint32_t j = 0; j < seg_count; ++j) {
            if (s.empty()) return std::nullopt;
            PathSegment seg{};
            if (s[0] > static_cast<uint8_t>(PathSegment::Type::Close)) return std::nullopt;
            seg.type = static_cast<PathSegment::Type>(s[0]);
            s = s.subspan(1);
            if (!ReadDouble(s, seg.p1.x)) return std::nullopt;
            if (!ReadDouble(s, seg.p1.y)) return std::nullopt;
            if (!ReadDouble(s, seg.p2.x)) return std::nullopt;
            if (!ReadDouble(s, seg.p2.y)) return std::nullopt;
            if (!ReadDouble(s, seg.p3.x)) return std::nullopt;
            if (!ReadDouble(s, seg.p3.y)) return std::nullopt;
            obj.segments.push_back(seg);
        }
        resp.objects.push_back(std::move(obj));
    }

    uint32_t report_count = 0;
    if (!ReadUint32(s, report_count)) return std::nullopt;
    if (report_count > s.size()/4) return std::nullopt;
    resp.loss_reports.reserve(report_count);
    for (uint32_t k = 0; k < report_count; ++k) {
        std::string rep;
        if (!ReadString(s, rep)) return std::nullopt;
        resp.loss_reports.push_back(std::move(rep));
    }

    if (!s.empty()) return std::nullopt;
    if (!PayloadValidator::ValidateGraphicPayload(resp.objects).IsOk()) return std::nullopt;
    return resp;
}

std::vector<uint8_t> SerializeCanaryRequest(const CanaryPayloadRequest& req, uint32_t sequence_id) {
    std::vector<uint8_t> payload;
    WriteUint32(payload, req.canary_type);
    WriteString(payload, req.target_path);
    WriteString(payload, req.network_address);
    WriteUint16(payload, req.network_port);
    WriteUint64(payload, req.test_handle_value);

    IpcHeader hdr{};
    hdr.magic = IPC_MAGIC;
    hdr.wire_version = IPC_WIRE_VERSION;
    hdr.message_type = static_cast<uint16_t>(MessageType::CanaryRequest);
    hdr.sequence_id = sequence_id;
    EnsureAppend(payload, 0);
    hdr.payload_bytes = static_cast<uint32_t>(payload.size());

    std::vector<uint8_t> packet(sizeof(IpcHeader));
    std::memcpy(packet.data(), &hdr, sizeof(IpcHeader));
    packet.insert(packet.end(), payload.begin(), payload.end());
    return packet;
}

std::optional<CanaryPayloadRequest> DeserializeCanaryRequest(std::span<const uint8_t> data) {
    if (data.size() < sizeof(IpcHeader)) return std::nullopt;
    IpcHeader hdr{};
    std::memcpy(&hdr, data.data(), sizeof(IpcHeader));
    if (hdr.magic != IPC_MAGIC || hdr.wire_version != IPC_WIRE_VERSION) return std::nullopt;
    if (hdr.message_type != static_cast<uint16_t>(MessageType::CanaryRequest)) return std::nullopt;
    if (hdr.payload_bytes > IPC_MAX_PAYLOAD_BYTES || data.size() != sizeof(IpcHeader) + hdr.payload_bytes) return std::nullopt;

    std::span<const uint8_t> s = data.subspan(sizeof(IpcHeader), hdr.payload_bytes);
    CanaryPayloadRequest req{};
    if (!ReadUint32(s, req.canary_type)) return std::nullopt;
    if (!ReadString(s, req.target_path)) return std::nullopt;
    if (!ReadString(s, req.network_address)) return std::nullopt;
    if (!ReadUint16(s, req.network_port)) return std::nullopt;
    if (!ReadUint64(s, req.test_handle_value)) return std::nullopt;
    if (!s.empty()) return std::nullopt;
    return req;
}

std::vector<uint8_t> SerializeCanaryResponse(const CanaryPayloadResponse& resp, uint32_t sequence_id) {
    std::vector<uint8_t> payload;
    WriteUint32(payload, resp.canary_type);
    payload.push_back(resp.action_attempted ? 1 : 0);
    payload.push_back(resp.denied_by_os ? 1 : 0);
    WriteUint32(payload, resp.os_error_code);
    WriteString(payload, resp.diagnostic_message);

    IpcHeader hdr{};
    hdr.magic = IPC_MAGIC;
    hdr.wire_version = IPC_WIRE_VERSION;
    hdr.message_type = static_cast<uint16_t>(MessageType::CanaryResponse);
    hdr.sequence_id = sequence_id;
    EnsureAppend(payload, 0);
    hdr.payload_bytes = static_cast<uint32_t>(payload.size());

    std::vector<uint8_t> packet(sizeof(IpcHeader));
    std::memcpy(packet.data(), &hdr, sizeof(IpcHeader));
    packet.insert(packet.end(), payload.begin(), payload.end());
    return packet;
}

std::optional<CanaryPayloadResponse> DeserializeCanaryResponse(std::span<const uint8_t> data) {
    if (data.size() < sizeof(IpcHeader)) return std::nullopt;
    IpcHeader hdr{};
    std::memcpy(&hdr, data.data(), sizeof(IpcHeader));
    if (hdr.magic != IPC_MAGIC || hdr.wire_version != IPC_WIRE_VERSION) return std::nullopt;
    if (hdr.message_type != static_cast<uint16_t>(MessageType::CanaryResponse)) return std::nullopt;
    if (hdr.payload_bytes > IPC_MAX_PAYLOAD_BYTES || data.size() != sizeof(IpcHeader) + hdr.payload_bytes) return std::nullopt;

    std::span<const uint8_t> s = data.subspan(sizeof(IpcHeader), hdr.payload_bytes);
    CanaryPayloadResponse resp{};
    if (!ReadUint32(s, resp.canary_type)) return std::nullopt;
    if (s.size() < 2 || s[0] > 1 || s[1] > 1) return std::nullopt;
    resp.action_attempted = (s[0] != 0);
    resp.denied_by_os = (s[1] != 0);
    s = s.subspan(2);
    if (!ReadUint32(s, resp.os_error_code)) return std::nullopt;
    if (!ReadString(s, resp.diagnostic_message)) return std::nullopt;
    if (!s.empty()) return std::nullopt;
    return resp;
}

} // namespace pikdraw::interop
