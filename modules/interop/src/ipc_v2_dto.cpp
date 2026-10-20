#include "pikdraw/interop/ipc_dto.hpp"

#include <algorithm>
#include <cstring>

namespace pikdraw::interop {
namespace {

class Writer final {
public:
    bool U16(uint16_t value) { return Integer(value, 2); }
    bool U32(uint32_t value) { return Integer(value, 4); }
    bool U64(uint64_t value) { return Integer(value, 8); }
    bool Bytes(std::span<const uint8_t> bytes) {
        if (bytes.size() > IPC_V2_MAX_FRAME_BYTES - data_.size()) return false;
        data_.insert(data_.end(), bytes.begin(), bytes.end());
        return true;
    }
    bool String16(const std::string& value, size_t maximum) {
        return !value.empty() && value.size() <= maximum && U16(static_cast<uint16_t>(value.size())) &&
               Bytes(std::span(reinterpret_cast<const uint8_t*>(value.data()), value.size()));
    }
    [[nodiscard]] std::span<const uint8_t> View() const noexcept { return data_; }
    [[nodiscard]] std::vector<uint8_t> Take() && { return std::move(data_); }

private:
    bool Integer(uint64_t value, size_t bytes) {
        if (bytes > IPC_V2_MAX_FRAME_BYTES - data_.size()) return false;
        for (size_t i = 0; i < bytes; ++i) {
            data_.push_back(static_cast<uint8_t>((value >> (i * 8)) & 0xff));
        }
        return true;
    }
    std::vector<uint8_t> data_;
};

class Reader final {
public:
    explicit Reader(std::span<const uint8_t> bytes) : bytes_(bytes) {}
    bool U16(uint16_t& value) {
        uint64_t decoded = 0;
        if (!Integer(decoded, 2)) return false;
        value = static_cast<uint16_t>(decoded);
        return true;
    }
    bool U32(uint32_t& value) {
        uint64_t decoded = 0;
        if (!Integer(decoded, 4)) return false;
        value = static_cast<uint32_t>(decoded);
        return true;
    }
    bool U64(uint64_t& value) { return Integer(value, 8); }
    bool Bytes(std::span<uint8_t> output) {
        if (output.size() > bytes_.size()) return false;
        std::copy_n(bytes_.begin(), output.size(), output.begin());
        bytes_ = bytes_.subspan(output.size());
        return true;
    }
    bool String16(std::string& value, size_t maximum) {
        uint16_t length = 0;
        if (!U16(length) || length == 0 || length > maximum || length > bytes_.size()) return false;
        value.assign(reinterpret_cast<const char*>(bytes_.data()), length);
        bytes_ = bytes_.subspan(length);
        return std::none_of(value.begin(), value.end(), [](unsigned char c) { return c == 0; });
    }
    [[nodiscard]] std::span<const uint8_t> Remaining() const noexcept { return bytes_; }

private:
    bool Integer(uint64_t& value, size_t bytes) {
        if (bytes > bytes_.size()) return false;
        value = 0;
        for (size_t i = 0; i < bytes; ++i) value |= static_cast<uint64_t>(bytes_[i]) << (i * 8);
        bytes_ = bytes_.subspan(bytes);
        return true;
    }
    std::span<const uint8_t> bytes_;
};

std::optional<std::span<const uint8_t>> Payload(
    std::span<const uint8_t> packet, MessageType expected) {
    if (packet.size() < sizeof(IpcHeader)) return std::nullopt;
    IpcHeader header{};
    std::memcpy(&header, packet.data(), sizeof(header));
    if (!ValidateV2FrameHeader(header) || header.message_type != static_cast<uint16_t>(expected) ||
        packet.size() != sizeof(IpcHeader) + header.payload_bytes) return std::nullopt;
    return packet.subspan(sizeof(IpcHeader), header.payload_bytes);
}

bool WriteIdentity(Writer& writer, const V2JobIdentity& job) {
    if (job.document_id.empty() || job.document_id.size() > IPC_V2_MAX_DOCUMENT_ID_BYTES || job.generation == 0 ||
        std::any_of(job.source_token.begin(), job.source_token.end(), [](uint8_t b) { return b != 0; }) == false) {
        return false;
    }
    return writer.String16(job.document_id, IPC_V2_MAX_DOCUMENT_ID_BYTES) &&
           writer.U64(job.epoch) && writer.U64(job.base_revision) && writer.U64(job.generation) &&
           writer.Bytes(job.source_token);
}

bool WriteIdentityPrefix(Writer& writer, const V2FrameIdentity& identity) {
    return writer.Bytes(identity.source_token) && writer.U64(identity.generation);
}

bool ReadIdentityPrefix(Reader& reader, V2FrameIdentity& identity) {
    return reader.Bytes(identity.source_token) && reader.U64(identity.generation);
}

bool PrefixMatches(const V2FrameIdentity& prefix, const V2JobIdentity& job) {
    return prefix.source_token == job.source_token && prefix.generation == job.generation;
}

bool ReadIdentity(Reader& reader, V2JobIdentity& job) {
    return reader.String16(job.document_id, IPC_V2_MAX_DOCUMENT_ID_BYTES) &&
           reader.U64(job.epoch) && reader.U64(job.base_revision) && reader.U64(job.generation) &&
           reader.Bytes(job.source_token) &&
           job.generation != 0 &&
           std::any_of(job.source_token.begin(), job.source_token.end(), [](uint8_t b) { return b != 0; });
}

bool ReadFramePayload(std::span<const uint8_t> packet, MessageType type, Reader& reader) {
    const auto payload = Payload(packet, type);
    if (!payload) return false;
    reader = Reader(*payload);
    return true;
}

bool ValidateCanonicalRecords(
    std::span<const uint8_t> bytes,
    uint32_t expected_count,
    std::vector<uint16_t>* types) {
    if (expected_count == 0 || expected_count > IPC_V2_MAX_RECORDS_PER_CHUNK) return false;
    Reader records(bytes);
    if (types) types->clear();
    for (uint32_t i = 0; i < expected_count; ++i) {
        uint16_t type = 0;
        uint16_t flags = 0;
        uint32_t length = 0;
        if (!records.U16(type) || !records.U16(flags) || !records.U32(length) ||
            type == 0 || flags != 0 || length > IPC_V2_MAX_RECORD_BYTES ||
            length > records.Remaining().size()) return false;
        if (types) types->push_back(type);
        records = Reader(records.Remaining().subspan(length));
    }
    return records.Remaining().empty();
}

} // namespace

uint32_t V2ProposalRecordsCrc32(std::span<const uint8_t> bytes) noexcept {
    uint32_t crc = 0xffffffffu;
    for (uint8_t byte : bytes) {
        crc ^= byte;
        for (int bit = 0; bit < 8; ++bit) {
            const uint32_t mask = 0u - (crc & 1u);
            crc = (crc >> 1) ^ (0xedb88320u & mask);
        }
    }
    return ~crc;
}

std::array<uint8_t, IPC_V2_IDENTITY_PREFIX_BYTES> SerializeV2FrameIdentityPrefix(
    const V2FrameIdentity& identity) noexcept {
    std::array<uint8_t, IPC_V2_IDENTITY_PREFIX_BYTES> bytes{};
    std::copy(identity.source_token.begin(), identity.source_token.end(), bytes.begin());
    for (size_t i = 0; i < sizeof(identity.generation); ++i) {
        bytes[identity.source_token.size() + i] =
            static_cast<uint8_t>((identity.generation >> (i * 8)) & 0xff);
    }
    return bytes;
}

std::optional<V2FrameIdentity> DeserializeV2FrameIdentityPrefix(
    std::span<const uint8_t> bytes) noexcept {
    if (bytes.size() != IPC_V2_IDENTITY_PREFIX_BYTES) return std::nullopt;
    V2FrameIdentity identity;
    std::copy_n(bytes.begin(), identity.source_token.size(), identity.source_token.begin());
    for (size_t i = 0; i < sizeof(identity.generation); ++i) {
        identity.generation |= static_cast<uint64_t>(bytes[identity.source_token.size() + i]) << (i * 8);
    }
    return identity;
}

bool AppendV2ProposalRecord(
    std::vector<uint8_t>& records,
    uint16_t record_type,
    std::span<const uint8_t> payload) {
    if (record_type == 0 || payload.size() > IPC_V2_MAX_RECORD_BYTES ||
        records.size() > IPC_V2_MAX_FRAME_BYTES - 8 ||
        payload.size() > IPC_V2_MAX_FRAME_BYTES - records.size() - 8) return false;
    Writer header;
    if (!header.U16(record_type) || !header.U16(0) || !header.U32(static_cast<uint32_t>(payload.size()))) return false;
    const auto encoded = header.View();
    records.insert(records.end(), encoded.begin(), encoded.end());
    records.insert(records.end(), payload.begin(), payload.end());
    return true;
}

std::vector<uint8_t> SerializeV2NegotiationRequest(
    const V2NegotiationRequest& request, uint32_t sequence_id) {
    if (request.major != IPC_WIRE_VERSION_V2 || request.required_features == 0 ||
        (request.required_features & ~IPC_V2_SUPPORTED_FEATURES) != 0) return {};
    Writer writer;
    if (!WriteIdentityPrefix(writer, {}) || !writer.U16(request.major) || !writer.U16(request.minor) ||
        !writer.U32(request.required_features)) return {};
    return SerializeV2Frame(MessageType::V2NegotiateRequest, sequence_id, writer.View());
}

std::optional<V2NegotiationRequest> DeserializeV2NegotiationRequest(std::span<const uint8_t> packet) {
    Reader reader({});
    if (!ReadFramePayload(packet, MessageType::V2NegotiateRequest, reader)) return std::nullopt;
    V2FrameIdentity prefix;
    V2NegotiationRequest request;
    if (!ReadIdentityPrefix(reader, prefix) || !prefix.IsZero() || !reader.U16(request.major) ||
        !reader.U16(request.minor) || !reader.U32(request.required_features) ||
        !reader.Remaining().empty() || request.major != IPC_WIRE_VERSION_V2 || request.required_features == 0 ||
        (request.required_features & ~IPC_V2_SUPPORTED_FEATURES) != 0) return std::nullopt;
    return request;
}

std::vector<uint8_t> SerializeV2NegotiationResponse(
    const V2NegotiationResponse& response, uint32_t sequence_id) {
    if ((response.accepted && (response.major != IPC_WIRE_VERSION_V2 || response.error_code != 0)) ||
        (response.accepted_features & ~IPC_V2_SUPPORTED_FEATURES) != 0 ||
        (response.accepted && response.accepted_features == 0)) return {};
    Writer writer;
    if (!WriteIdentityPrefix(writer, {}) || !writer.U16(response.major) || !writer.U16(response.minor) ||
        !writer.U32(response.accepted_features) || !writer.U32(response.error_code) ||
        !writer.U16(response.accepted ? 1 : 0) || !writer.U16(0)) return {};
    return SerializeV2Frame(MessageType::V2NegotiateResponse, sequence_id, writer.View());
}

std::optional<V2NegotiationResponse> DeserializeV2NegotiationResponse(std::span<const uint8_t> packet) {
    Reader reader({});
    if (!ReadFramePayload(packet, MessageType::V2NegotiateResponse, reader)) return std::nullopt;
    V2FrameIdentity prefix;
    V2NegotiationResponse response;
    uint16_t accepted = 0;
    uint16_t reserved = 0;
    if (!ReadIdentityPrefix(reader, prefix) || !prefix.IsZero() ||
        !reader.U16(response.major) || !reader.U16(response.minor) ||
        !reader.U32(response.accepted_features) || !reader.U32(response.error_code) ||
        !reader.U16(accepted) || !reader.U16(reserved) || !reader.Remaining().empty() ||
        accepted > 1 || reserved != 0 || (response.accepted_features & ~IPC_V2_SUPPORTED_FEATURES) != 0) {
        return std::nullopt;
    }
    response.accepted = accepted == 1;
    if ((response.accepted && (response.major != IPC_WIRE_VERSION_V2 || response.error_code != 0 ||
                               response.accepted_features == 0)) ||
        (!response.accepted && response.error_code == 0)) return std::nullopt;
    return response;
}

std::vector<uint8_t> SerializeV2ImportBegin(const V2ImportBegin& request, uint32_t sequence_id) {
    if (request.source_bytes == 0 || request.format_id.empty() || request.profile_id.empty()) return {};
    Writer writer;
    const V2FrameIdentity prefix{request.job.source_token, request.job.generation};
    if (!WriteIdentityPrefix(writer, prefix) || !WriteIdentity(writer, request.job) || !writer.U64(request.source_bytes) ||
        !writer.Bytes(request.source_sha256) || !writer.String16(request.format_id, IPC_V2_MAX_FORMAT_ID_BYTES) ||
        !writer.String16(request.profile_id, IPC_V2_MAX_PROFILE_ID_BYTES)) return {};
    if (request.source_page_index && !writer.U32(*request.source_page_index)) return {};
    return SerializeV2Frame(MessageType::V2ImportBegin, sequence_id, writer.View());
}

std::optional<V2ImportBegin> DeserializeV2ImportBegin(std::span<const uint8_t> packet) {
    Reader reader({});
    if (!ReadFramePayload(packet, MessageType::V2ImportBegin, reader)) return std::nullopt;
    V2FrameIdentity prefix;
    V2ImportBegin request;
    if (!ReadIdentityPrefix(reader, prefix) || !ReadIdentity(reader, request.job) ||
        !PrefixMatches(prefix, request.job) || !reader.U64(request.source_bytes) || request.source_bytes == 0 ||
        !reader.Bytes(request.source_sha256) || !reader.String16(request.format_id, IPC_V2_MAX_FORMAT_ID_BYTES) ||
        !reader.String16(request.profile_id, IPC_V2_MAX_PROFILE_ID_BYTES)) {
        return std::nullopt;
    }
    if (!reader.Remaining().empty()) {
        uint32_t source_page_index = 0;
        if (!reader.U32(source_page_index) || !reader.Remaining().empty()) return std::nullopt;
        request.source_page_index = source_page_index;
    }
    return request;
}

std::vector<uint8_t> SerializeV2ProposalChunk(const V2ProposalChunk& chunk, uint32_t sequence_id) {
    std::vector<uint16_t> parsed_types;
    if (chunk.record_count == 0 || chunk.record_count > IPC_V2_MAX_RECORDS_PER_CHUNK ||
        chunk.canonical_record_bytes.size() > IPC_V2_MAX_FRAME_BYTES ||
        !ValidateCanonicalRecords(chunk.canonical_record_bytes, chunk.record_count, &parsed_types) ||
        V2ProposalRecordsCrc32(chunk.canonical_record_bytes) != chunk.checksum_crc32 ||
        (!chunk.record_types.empty() && chunk.record_types != parsed_types)) return {};
    Writer writer;
    const V2FrameIdentity prefix{chunk.job.source_token, chunk.job.generation};
    if (!WriteIdentityPrefix(writer, prefix) || !WriteIdentity(writer, chunk.job) || !writer.U32(chunk.chunk_ordinal) ||
        !writer.U64(chunk.first_record_ordinal) || !writer.U32(chunk.record_count) ||
        !writer.U32(static_cast<uint32_t>(chunk.canonical_record_bytes.size())) ||
        !writer.U32(chunk.checksum_crc32) || !writer.Bytes(chunk.canonical_record_bytes)) return {};
    return SerializeV2Frame(MessageType::V2ProposalChunk, sequence_id, writer.View());
}

std::optional<V2ProposalChunk> DeserializeV2ProposalChunk(std::span<const uint8_t> packet) {
    Reader reader({});
    if (!ReadFramePayload(packet, MessageType::V2ProposalChunk, reader)) return std::nullopt;
    V2FrameIdentity prefix;
    V2ProposalChunk chunk;
    uint32_t records_bytes = 0;
    if (!ReadIdentityPrefix(reader, prefix) || !ReadIdentity(reader, chunk.job) ||
        !PrefixMatches(prefix, chunk.job) || !reader.U32(chunk.chunk_ordinal) ||
        !reader.U64(chunk.first_record_ordinal) || !reader.U32(chunk.record_count) ||
        !reader.U32(records_bytes) || !reader.U32(chunk.checksum_crc32) ||
        records_bytes != reader.Remaining().size() || records_bytes > IPC_V2_MAX_FRAME_BYTES ||
        chunk.record_count == 0 || chunk.record_count > IPC_V2_MAX_RECORDS_PER_CHUNK) return std::nullopt;
    try {
        chunk.canonical_record_bytes.assign(reader.Remaining().begin(), reader.Remaining().end());
        if (!ValidateCanonicalRecords(chunk.canonical_record_bytes, chunk.record_count, &chunk.record_types) ||
            V2ProposalRecordsCrc32(chunk.canonical_record_bytes) != chunk.checksum_crc32) return std::nullopt;
    } catch (...) {
        return std::nullopt;
    }
    return chunk;
}

std::vector<uint8_t> SerializeV2ImportEnd(const V2ImportEnd& end, uint32_t sequence_id) {
    Writer writer;
    const V2FrameIdentity prefix{end.job.source_token, end.job.generation};
    if (!WriteIdentityPrefix(writer, prefix) || !WriteIdentity(writer, end.job) ||
        !writer.U32(end.chunk_count) || !writer.U64(end.record_count) ||
        !writer.U64(end.canonical_bytes) || !writer.U16(end.fidelity_report_complete ? 1 : 0) ||
        !writer.U16(0) || !writer.U32(0) || !writer.Bytes(end.canonical_sha256)) return {};
    return SerializeV2Frame(MessageType::V2ImportEnd, sequence_id, writer.View());
}

std::optional<V2ImportEnd> DeserializeV2ImportEnd(std::span<const uint8_t> packet) {
    Reader reader({});
    if (!ReadFramePayload(packet, MessageType::V2ImportEnd, reader)) return std::nullopt;
    V2FrameIdentity prefix;
    V2ImportEnd end;
    uint16_t complete = 0;
    uint16_t reserved16 = 0;
    uint32_t reserved32 = 0;
    if (!ReadIdentityPrefix(reader, prefix) || !ReadIdentity(reader, end.job) ||
        !PrefixMatches(prefix, end.job) || !reader.U32(end.chunk_count) || !reader.U64(end.record_count) ||
        !reader.U64(end.canonical_bytes) || !reader.U16(complete) || !reader.U16(reserved16) ||
        !reader.U32(reserved32) || !reader.Bytes(end.canonical_sha256) || !reader.Remaining().empty() ||
        complete > 1 || reserved16 != 0 || reserved32 != 0) return std::nullopt;
    end.fidelity_report_complete = complete == 1;
    return end;
}

std::vector<uint8_t> SerializeV2ErrorResponse(const V2ErrorResponse& error, uint32_t sequence_id) {
    if (error.error_code == 0 || error.diagnostic.empty() || error.diagnostic.size() > 256) return {};
    Writer writer;
    if (!WriteIdentityPrefix(writer, {}) || !writer.U32(error.error_code) ||
        !writer.String16(error.diagnostic, 256)) return {};
    return SerializeV2Frame(MessageType::V2ErrorResponse, sequence_id, writer.View());
}

std::optional<V2ErrorResponse> DeserializeV2ErrorResponse(std::span<const uint8_t> packet) {
    Reader reader({});
    if (!ReadFramePayload(packet, MessageType::V2ErrorResponse, reader)) return std::nullopt;
    V2FrameIdentity prefix;
    V2ErrorResponse error;
    if (!ReadIdentityPrefix(reader, prefix) || !prefix.IsZero() ||
        !reader.U32(error.error_code) || error.error_code == 0 ||
        !reader.String16(error.diagnostic, 256) || !reader.Remaining().empty()) return std::nullopt;
    return error;
}

} // namespace pikdraw::interop
