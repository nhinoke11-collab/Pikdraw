#include "pikdraw/document/storage/save_recovery.hpp"
#include "pikdraw/document/storage/native_format.hpp"
#include <fstream>
#include <filesystem>
#include <chrono>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace pikdraw::document {

namespace {

std::string generate_temp_path(const std::string& target_path) {
    auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    return target_path + ".tmp." + std::to_string(now);
}

bool write_all_bytes(const std::string& path, const std::vector<uint8_t>& data) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(reinterpret_cast<const char*>(data.data()), data.size());
    out.flush();
    return out.good();
}

bool atomic_replace_file(const std::string& temp_path, const std::string& target_path) {
#if defined(_WIN32)
    std::wstring wtemp(temp_path.begin(), temp_path.end());
    std::wstring wtarget(target_path.begin(), target_path.end());
    if (::MoveFileExW(wtemp.c_str(), wtarget.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        return true;
    }
#endif
    std::error_code ec;
    std::filesystem::rename(temp_path, target_path, ec);
    if (ec) {
        // If rename failed (e.g. target exists on some OS implementations)
        std::filesystem::remove(target_path, ec);
        std::filesystem::rename(temp_path, target_path, ec);
    }
    return !ec;
}

} // anonymous namespace

SaveResult SaveRecoveryService::atomic_save(
    const std::string& target_path,
    const DocumentSnapshot& snapshot) {
    SaveResult res;
    res.path = target_path;
    res.saved_epoch = snapshot.epoch();
    res.saved_revision = snapshot.revision();

    auto bytes = NativeFormat::serialize(snapshot);
    std::string temp_path = generate_temp_path(target_path);

    if (!write_all_bytes(temp_path, bytes)) {
        res.success = false;
        res.error_message = "Failed to write bytes to temporary file: " + temp_path;
        return res;
    }

    if (!atomic_replace_file(temp_path, target_path)) {
        res.success = false;
        res.error_message = "Failed to atomically replace destination file: " + target_path;
        std::error_code ec;
        std::filesystem::remove(temp_path, ec);
        return res;
    }

    res.success = true;
    return res;
}

std::shared_ptr<DocumentSnapshot> SaveRecoveryService::load(
    const std::string& path,
    std::string* out_error) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) {
        if (out_error) *out_error = "Could not open file: " + path;
        return nullptr;
    }

    std::streamsize size = in.tellg();
    in.seekg(0, std::ios::beg);

    if (size <= 0) {
        if (out_error) *out_error = "File is empty: " + path;
        return nullptr;
    }

    std::vector<uint8_t> buffer(static_cast<size_t>(size));
    if (!in.read(reinterpret_cast<char*>(buffer.data()), size)) {
        if (out_error) *out_error = "Failed to read file content: " + path;
        return nullptr;
    }

    return NativeFormat::deserialize(buffer.data(), buffer.size(), out_error);
}

bool SaveRecoveryService::write_recovery_checkpoint(
    const std::string& recovery_dir,
    const std::string& doc_id,
    const DocumentSnapshot& snapshot) {
    std::error_code ec;
    std::filesystem::create_directories(recovery_dir, ec);
    auto now = std::chrono::system_clock::now().time_since_epoch().count();
    std::string filename = "recovery_" + doc_id + "_" + std::to_string(now) +
                           "_rev" + std::to_string(snapshot.revision()) + ".pkdr";
    std::filesystem::path full_path = std::filesystem::path(recovery_dir) / filename;
    auto res = atomic_save(full_path.string(), snapshot);
    return res.success;
}

std::optional<RecoveryInfo> SaveRecoveryService::find_latest_checkpoint(
    const std::string& recovery_dir,
    const std::string& doc_id) {
    std::error_code ec;
    if (!std::filesystem::exists(recovery_dir, ec)) return std::nullopt;

    std::string prefix = "recovery_" + doc_id + "_";
    std::optional<RecoveryInfo> latest;

    for (const auto& entry : std::filesystem::directory_iterator(recovery_dir, ec)) {
        if (!entry.is_regular_file()) continue;
        std::string name = entry.path().filename().string();
        if (name.find(prefix) == 0 && entry.path().extension() == ".pkdr") {
            auto snap = load(entry.path().string());
            if (snap) {
                if (!latest || snap->revision() > latest->revision) {
                    RecoveryInfo info;
                    info.path = entry.path().string();
                    info.epoch = snap->epoch();
                    info.revision = snap->revision();
                    info.timestamp = name;
                    latest = info;
                }
            }
        }
    }
    return latest;
}

void SaveRecoveryService::cleanup_checkpoints(
    const std::string& recovery_dir,
    const std::string& doc_id) {
    std::error_code ec;
    if (!std::filesystem::exists(recovery_dir, ec)) return;

    std::string prefix = "recovery_" + doc_id + "_";
    for (const auto& entry : std::filesystem::directory_iterator(recovery_dir, ec)) {
        if (entry.is_regular_file()) {
            std::string name = entry.path().filename().string();
            if (name.find(prefix) == 0 && entry.path().extension() == ".pkdr") {
                std::filesystem::remove(entry.path(), ec);
            }
        }
    }
}

} // namespace pikdraw::document
