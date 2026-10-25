#include "native_window_automation.hpp"

#include "pikdraw/desktop/shell/native_window.hpp"
#include "pikdraw/document/storage/save_recovery.hpp"
#include "pikdraw/document/storage/native_file.hpp"
#include "pikdraw/geometry/units/length_unit.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <initializer_list>
#include <iostream>
#include <exception>
#include <set>
#include <type_traits>
#include <unordered_set>
#include <variant>

#ifdef _WIN32
#include <windows.h>
#endif

namespace pikdraw::desktop {
namespace {

std::atomic<uint64_t> next_agent_document_id{1};

const std::string* string_field(const automation::JsonValue& value, std::string_view name) {
    const auto* field = value.get(name);
    return field ? field->string() : nullptr;
}

automation::JsonValue error(std::string code, std::string message) {
    return automation::JsonValue::Object{{"status", "error"}, {"code", std::move(code)},
                                         {"message", std::move(message)}};
}

bool only_fields(const automation::JsonValue& value,
                 std::initializer_list<std::string_view> allowed) {
    const auto* object = value.object();
    if (!object) return false;
    for (const auto& [key, ignored] : *object) {
        (void)ignored;
        if (std::find(allowed.begin(), allowed.end(), key) == allowed.end()) return false;
    }
    return true;
}

bool resolve_automation_file(const std::string& file_name, bool create_directory,
                             std::string& utf8_path, std::string& diagnostic) {
    if (file_name.empty() || file_name.size() > 100 || file_name == "." || file_name == ".." ||
        file_name.find("..") != std::string::npos || file_name.find_first_of("/\\:") != std::string::npos ||
        file_name.front() == '.') {
        diagnostic = "file_name must be a safe basename of at most 100 ASCII characters.";
        return false;
    }
    for (const unsigned char value : file_name) {
        if (!(std::isalnum(value) || value == ' ' || value == '_' || value == '-' ||
              value == '(' || value == ')' || value == '.')) {
            diagnostic = "file_name contains a character outside the permitted ASCII set.";
            return false;
        }
    }
    std::string name = file_name;
    const auto extension = std::filesystem::path(name).extension().string();
    if (extension.empty()) name += document::kNativeDocumentExtension;
    else {
        if (!document::is_native_document_path(std::filesystem::path(name))) {
            diagnostic = "Only native .pr files are available through this grant.";
            return false;
        }
    }
    auto stem = std::filesystem::path(name).stem().string();
    std::transform(stem.begin(), stem.end(), stem.begin(),
        [](unsigned char value) { return static_cast<char>(std::toupper(value)); });
    if (stem == "CON" || stem == "PRN" || stem == "AUX" || stem == "NUL" ||
        (stem.size() == 4 && (stem.starts_with("COM") || stem.starts_with("LPT")) &&
         stem[3] >= '1' && stem[3] <= '9')) {
        diagnostic = "file_name is reserved by Windows.";
        return false;
    }
#ifdef _WIN32
    const DWORD size = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
    if (size == 0) { diagnostic = "LOCALAPPDATA is unavailable."; return false; }
    std::wstring local_app_data(size, L'\0');
    const DWORD copied = GetEnvironmentVariableW(L"LOCALAPPDATA", local_app_data.data(), size);
    if (copied == 0 || copied >= size) { diagnostic = "Could not read LOCALAPPDATA."; return false; }
    local_app_data.resize(copied);
    const auto root = std::filesystem::path(local_app_data) / L"PikDraw" / L"automation" / L"files";
    std::error_code ec;
    if (create_directory) std::filesystem::create_directories(root, ec);
    if (ec || !std::filesystem::is_directory(root, ec) || ec) {
        diagnostic = "The local automation files folder is unavailable.";
        return false;
    }
    utf8_path = NativeWindow::wide_to_utf8((root / NativeWindow::utf8_to_wide(name)).wstring());
    if (utf8_path.empty()) { diagnostic = "Could not encode the native file path as UTF-8."; return false; }
    return true;
#else
    (void)create_directory;
    (void)utf8_path;
    diagnostic = "Native automation files are available only on Windows.";
    return false;
#endif
}

#ifdef _WIN32
class AutomationFileReadGuard final {
public:
    ~AutomationFileReadGuard() {
        if (file_ != INVALID_HANDLE_VALUE) CloseHandle(file_);
        if (folder_ != INVALID_HANDLE_VALUE) CloseHandle(folder_);
    }

    bool open(const std::string& utf8_path, std::string& diagnostic) {
        const auto path = NativeWindow::utf8_to_wide(utf8_path);
        if (path.empty()) {
            diagnostic = "The native file path could not be decoded.";
            return false;
        }
        const auto folder = std::filesystem::path(path).parent_path();
        folder_ = CreateFileW(folder.c_str(), FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (folder_ == INVALID_HANDLE_VALUE) {
            diagnostic = "The automation files folder could not be opened safely.";
            return false;
        }
        BY_HANDLE_FILE_INFORMATION folder_info{};
        if (!GetFileInformationByHandle(folder_, &folder_info) ||
            (folder_info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 ||
            (folder_info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
            diagnostic = "The automation files folder is not a regular directory.";
            return false;
        }

        file_ = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
            OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (file_ == INVALID_HANDLE_VALUE) {
            diagnostic = "The native file could not be opened for reading.";
            return false;
        }
        BY_HANDLE_FILE_INFORMATION file_info{};
        if (!GetFileInformationByHandle(file_, &file_info) ||
            (file_info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 ||
            (file_info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            diagnostic = "Reparse points and directories are not valid automation files.";
            return false;
        }

        std::wstring folder_final, file_final;
        if (!final_path(folder_, folder_final) || !final_path(file_, file_final) ||
            !is_child_path(folder_final, file_final)) {
            diagnostic = "The native file resolves outside the automation files grant.";
            return false;
        }
        return true;
    }

private:
    static bool final_path(HANDLE handle, std::wstring& result) {
        const DWORD needed = GetFinalPathNameByHandleW(handle, nullptr, 0,
            FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
        if (needed == 0 || needed > 32768) return false;
        result.resize(needed, L'\0');
        const DWORD copied = GetFinalPathNameByHandleW(handle, result.data(), needed,
            FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
        if (copied == 0 || copied >= needed) return false;
        result.resize(copied);
        while (result.size() > 4 && (result.back() == L'\\' || result.back() == L'/'))
            result.pop_back();
        return true;
    }

    static bool is_child_path(const std::wstring& folder, const std::wstring& file) {
        if (file.size() <= folder.size() || file[folder.size()] != L'\\') return false;
        return CompareStringOrdinal(folder.data(), static_cast<int>(folder.size()),
            file.data(), static_cast<int>(folder.size()), TRUE) == CSTR_EQUAL;
    }

    HANDLE folder_{INVALID_HANDLE_VALUE};
    HANDLE file_{INVALID_HANDLE_VALUE};
};

std::shared_ptr<document::DocumentSnapshot> load_granted_automation_file(
    const std::string& path, std::string& diagnostic) {
    AutomationFileReadGuard guard;
    if (!guard.open(path, diagnostic)) return {};
    auto snapshot = document::SaveRecoveryService::load(path, &diagnostic);
    if (!snapshot && diagnostic.empty()) diagnostic = "The native file could not be loaded.";
    return snapshot;
}
#else
std::shared_ptr<document::DocumentSnapshot> load_granted_automation_file(
    const std::string&, std::string& diagnostic) {
    diagnostic = "Native automation files are available only on Windows.";
    return {};
}
#endif

bool document_write_busy(const NativeWindow& window) {
    return window.is_inline_editing() || window.drawing_path_point_count() > 0 ||
           window.active_tool() == 8;
}

uint32_t current_process_id() noexcept {
#ifdef _WIN32
    return GetCurrentProcessId();
#else
    return 0;
#endif
}

} // namespace

bool NativeWindowAutomationRuntime::start(NativeWindow& window, bool edit_access,
                                          std::string& error_text) {
    granted_scopes = edit_access
        ? std::set<std::string>{"document.read", "document.create", "document.edit", "file.read", "file.write"}
        : std::set<std::string>{"document.read"};
    const std::string manifest_scopes = edit_access ? "read,edit,create,file-read,file-write" : "read";
#ifdef _WIN32
    if (!pipe.start(window.hwnd(), [&window, this](const automation::JsonValue& request) {
        return handle(window, request);
    }, manifest_scopes, error_text)) return false;

    automation::JsonValue::Array scope_values;
    for (const auto& scope : granted_scopes) scope_values.emplace_back(scope);
    const auto instance_id = std::to_string(current_process_id());
    const automation::JsonValue instance_list(automation::JsonValue::Object{
        {"status", "ok"},
        {"instances", automation::JsonValue::Array{automation::JsonValue(automation::JsonValue::Object{
            {"instance_id", instance_id}, {"granted_scopes", std::move(scope_values)}})}}});
    const auto instances = [instance_list] { return instance_list; };
    const auto app_call = [this, instance_id](std::string_view target,
        const automation::JsonValue& request, automation::JsonValue& response,
        std::string& error, const std::atomic<bool>* cancellation) {
        if (target != instance_id) { error = "The HTTP endpoint is scoped to another PikDraw instance."; return false; }
        return pipe.invoke(request, response, error, cancellation);
    };
    if (!http.start(instances, app_call, error_text)) {
        pipe.stop();
        return false;
    }
    return true;
#else
    return pipe.start([&window, this](const automation::JsonValue& request) {
        return handle(window, request);
    }, error_text);
#endif
}

automation::JsonValue NativeWindowAutomationRuntime::handle(
    NativeWindow& window, const automation::JsonValue& request) {
    std::unordered_set<document::Document*> live_documents;
    for (const auto& tab : window.tabs()) {
        if (tab.root) live_documents.insert(&tab.root->document());
    }
    std::erase_if(documents, [&live_documents](const auto& entry) {
        return !live_documents.contains(entry.first);
    });

    const auto* operation = string_field(request, "operation");
    if (!operation) return error("INVALID_ARGUMENT", "operation is required.");

    if (*operation == "document.create") {
        if (!only_fields(request, {"operation", "session_handle", "title", "page_width_mm", "page_height_mm",
                "page_width", "page_height", "length_unit"}))
            return error("INVALID_ARGUMENT", "document.create contains an unknown field.");
        const auto* session = string_field(request, "session_handle");
        if (!session) return error("SESSION_REQUIRED", "Connect to PikDraw with a document.create scope first.");
        Gateway* owner = nullptr;
        for (auto& [document, gateway] : documents) {
            (void)document;
            if (gateway && gateway->rpc.owns_session(*session)) {
                owner = gateway.get();
                break;
            }
        }
        if (!owner) return error("SESSION_EXPIRED", "The automation session is unknown or its document was closed.");
        if (!owner->rpc.has_scope(*session, "document.create"))
            return error("SCOPE_DENIED", "This session was not granted document.create.");
        const auto* width_mm = request.get("page_width_mm");
        const auto* height_mm = request.get("page_height_mm");
        const auto* width = request.get("page_width");
        const auto* height = request.get("page_height");
        const auto* unit_text = string_field(request, "length_unit");
        const bool has_mm = width_mm || height_mm;
        const bool has_typed = width || height;
        double page_width_mm = 0.0;
        double page_height_mm = 0.0;
        bool dimensions_valid = false;
        if (has_mm && !has_typed && !request.get("length_unit") &&
            width_mm && width_mm->number() && height_mm && height_mm->number() &&
            std::isfinite(*width_mm->number()) && std::isfinite(*height_mm->number())) {
            page_width_mm = *width_mm->number();
            page_height_mm = *height_mm->number();
            dimensions_valid = true;
        } else if (has_typed && !has_mm && unit_text && width && width->number() &&
                   height && height->number() && std::isfinite(*width->number()) &&
                   std::isfinite(*height->number())) {
            const auto unit = geometry::units::parse_length_unit_suffix(*unit_text);
            dimensions_valid = unit && geometry::units::is_physical_length_unit(*unit) &&
                geometry::units::convert_length_to_mm(*width->number(), *unit, page_width_mm) &&
                geometry::units::convert_length_to_mm(*height->number(), *unit, page_height_mm);
        }
        const auto* title = string_field(request, "title");
        if ((request.get("title") && !title) || !dimensions_valid ||
            page_width_mm < 10.0 || page_width_mm > 20000.0 ||
            page_height_mm < 10.0 || page_height_mm > 20000.0 ||
            (title && title->size() > 128))
            return error("INVALID_ARGUMENT", "Provide either page_width_mm/page_height_mm or page_width/page_height with a physical length_unit; dimensions must resolve to [10, 20000] mm and title, when provided, must be a UTF-8 string of at most 128 bytes.");
        if (window.tabs().size() >= 32) return error("RESOURCE_LIMIT", "At most 32 document tabs may be open.");
        if (document_write_busy(window)) return error("BUSY_INTERACTION", "Finish the current text or path interaction before creating a document.");
        window.create_new_tab(title ? *title : "AI Design", page_width_mm, page_height_mm);
        auto& created_document = window.active_tab().root->document();
        const auto initial = created_document.current_snapshot();
        const auto document_id = "agent-" + std::to_string(current_process_id()) + "-" +
            window.active_tab().id + "-" + std::to_string(next_agent_document_id.fetch_add(1));
        const auto identity_snapshot = std::make_shared<const document::DocumentSnapshot>(
            initial->epoch(), initial->revision(), initial->page(), initial->objects(), document_id);
        if (!created_document.reset_from_snapshot(identity_snapshot))
            return error("INTERNAL_ERROR", "The new automation document could not establish a unique document identity.");
        const auto state = created_document.current_snapshot();
        return automation::JsonValue::Object{{"status", "ok"}, {"document_id", state->document_id()},
            {"epoch", std::to_string(state->epoch())}, {"revision", std::to_string(state->revision())},
            {"page_width_mm", state->page().width_mm}, {"page_height_mm", state->page().height_mm},
            {"tab_count", static_cast<double>(window.tab_count())},
            {"instructions", "Connect to the new active document with pikdraw.connect before editing."}};
    }

    if (*operation == "session.create") {
        auto& tab = window.active_tab();
        if (!tab.root) return error("DOCUMENT_CLOSED", "There is no active document.");
        auto* document = &tab.root->document();
        auto [it, inserted] = documents.try_emplace(document);
        if (inserted) {
            it->second = std::make_unique<Gateway>(*document, granted_scopes);
        }
        return it->second->rpc.handle(request);
    }

    const auto* session = string_field(request, "session_handle");
    if (!session) return error("SESSION_REQUIRED", "Connect to a PikDraw instance first.");
    for (auto it = documents.begin(); it != documents.end(); ++it) {
        if (!it->second->rpc.owns_session(*session)) continue;
        if (*operation == "file.save") {
            if (!only_fields(request, {"operation", "session_handle", "file_name"}))
                return error("INVALID_ARGUMENT", "file.save contains an unknown field.");
            if (!it->second->rpc.has_scope(*session, "file.write"))
                return error("SCOPE_DENIED", "This session was not granted file.write.");
            const auto* file_name = string_field(request, "file_name");
            if (!file_name) return error("INVALID_ARGUMENT", "file_name is required.");
            std::string path, diagnostic;
            if (!resolve_automation_file(*file_name, true, path, diagnostic))
                return error("PATH_DENIED", std::move(diagnostic));
            std::error_code exists_error;
            if (std::filesystem::exists(std::filesystem::path(NativeWindow::utf8_to_wide(path)), exists_error) || exists_error)
                return error("PATH_DENIED", "The destination already exists or cannot be inspected; overwrite is not granted.");
            auto snapshot = it->first->current_snapshot();
            if (!snapshot) return error("DOCUMENT_CLOSED", "No committed document snapshot is available.");
            const auto saved = document::SaveRecoveryService::atomic_save(path, *snapshot);
            if (!saved.success) return error("FILE_IO_ERROR", saved.error_message);
            it->first->mark_saved(saved.saved_epoch, saved.saved_revision);
            if (window.active_tab().root && &window.active_tab().root->document() == it->first)
                window.active_tab().file_path = path;
            return automation::JsonValue::Object{{"status", "ok"}, {"api_version", std::string(automation::kApiVersion)},
                {"document_id", snapshot->document_id()}, {"epoch", std::to_string(saved.saved_epoch)},
                {"revision", std::to_string(saved.saved_revision)}, {"file_name", *file_name},
                {"path", path}, {"dirty", it->first->is_dirty()}};
        }
        if (*operation == "file.verify_reopen") {
            if (!only_fields(request, {"operation", "session_handle", "file_name"}))
                return error("INVALID_ARGUMENT", "file.verify_reopen contains an unknown field.");
            if (!it->second->rpc.has_scope(*session, "file.read"))
                return error("SCOPE_DENIED", "This session was not granted file.read.");
            const auto* file_name = string_field(request, "file_name");
            if (!file_name) return error("INVALID_ARGUMENT", "file_name is required.");
            std::string path, diagnostic;
            if (!resolve_automation_file(*file_name, false, path, diagnostic))
                return error("PATH_DENIED", std::move(diagnostic));
            std::string load_error;
            const auto reopened = load_granted_automation_file(path, load_error);
            if (!reopened) return error("FILE_INVALID", load_error);
            automation::JsonValue::Array ids;
            automation::JsonValue::Object type_counts;
            for (const auto& layer : reopened->page().layers) {
                for (const auto id : layer.object_ids) {
                    const auto object = reopened->get_object(id);
                    if (!object) continue;
                    ids.emplace_back(std::to_string(id));
                    std::string type = std::visit([](const auto& value) -> std::string {
                        using T = std::decay_t<decltype(value)>;
                        if constexpr (std::is_same_v<T, document::PathObject>) return "path";
                        if constexpr (std::is_same_v<T, document::TextObject>) return "text";
                        if constexpr (std::is_same_v<T, document::GroupObject>) return "group";
                        return "image";
                    }, *object);
                    const auto* existing = type_counts.contains(type) ? type_counts[type].number() : nullptr;
                    type_counts[type] = (existing ? *existing : 0.0) + 1.0;
                }
            }
            return automation::JsonValue::Object{{"status", "ok"}, {"reopened", true},
                {"document_id", reopened->document_id()}, {"epoch", std::to_string(reopened->epoch())},
                {"revision", std::to_string(reopened->revision())}, {"page_width_mm", reopened->page().width_mm},
                {"page_height_mm", reopened->page().height_mm},
                {"layer_count", static_cast<double>(reopened->page().layers.size())},
                {"object_count", static_cast<double>(reopened->object_count())},
                {"object_ids", std::move(ids)}, {"object_type_counts", std::move(type_counts)}};
        }
        if (*operation == "file.open") {
            if (!only_fields(request, {"operation", "session_handle", "file_name"}))
                return error("INVALID_ARGUMENT", "file.open contains an unknown field.");
            if (!it->second->rpc.has_scope(*session, "file.read"))
                return error("SCOPE_DENIED", "This session was not granted file.read.");
            const auto* file_name = string_field(request, "file_name");
            if (!file_name) return error("INVALID_ARGUMENT", "file_name is required.");
            if (window.tabs().size() >= 32)
                return error("RESOURCE_LIMIT", "At most 32 document tabs may be open.");
            if (document_write_busy(window))
                return error("BUSY_INTERACTION", "Finish the current text or path interaction before opening a document tab.");
            std::string path, diagnostic;
            if (!resolve_automation_file(*file_name, false, path, diagnostic))
                return error("PATH_DENIED", std::move(diagnostic));
            std::string load_error;
            const auto loaded = load_granted_automation_file(path, load_error);
            if (!loaded) return error("FILE_INVALID", load_error);

            const auto title = std::filesystem::path(*file_name).stem().string();
            window.create_new_tab(title, loaded->page().width_mm, loaded->page().height_mm);
            auto& tab = window.active_tab();
            auto& opened_document = tab.root->document();
            const auto document_id = "agent-" + std::to_string(current_process_id()) + "-" +
                tab.id + "-" + std::to_string(next_agent_document_id.fetch_add(1));
            const auto identity_snapshot = std::make_shared<const document::DocumentSnapshot>(
                loaded->epoch(), loaded->revision(), loaded->page(), loaded->objects(), document_id);
            if (!opened_document.reset_from_snapshot(identity_snapshot)) {
                window.close_tab(window.active_tab_index());
                return error("INTERNAL_ERROR", "The native file could not be installed into a new document tab.");
            }
            const auto state = opened_document.current_snapshot();
            tab.title = title;
            tab.file_path = path;
            tab.page_width_mm = state->page().width_mm;
            tab.page_height_mm = state->page().height_mm;
            tab.is_dirty = false;
            return automation::JsonValue::Object{{"status", "ok"},
                {"api_version", std::string(automation::kApiVersion)},
                {"document_id", state->document_id()}, {"epoch", std::to_string(state->epoch())},
                {"revision", std::to_string(state->revision())}, {"file_name", *file_name},
                {"tab_id", tab.id}, {"tab_count", static_cast<double>(window.tab_count())},
                {"page_width_mm", state->page().width_mm}, {"page_height_mm", state->page().height_mm},
                {"layer_count", static_cast<double>(state->page().layers.size())},
                {"object_count", static_cast<double>(state->object_count())}, {"dirty", false},
                {"instructions", "Connect to this new document with pikdraw.connect before editing."}};
        }
        if ((*operation == "batch.apply" || *operation == "object.create_rectangle") && document_write_busy(window))
            return error("BUSY_INTERACTION", "Finish the current text or path interaction before applying document edits.");
        auto result = it->second->rpc.handle(request);
        if (*operation == "session.disconnect" && it->second->rpc.session_count() == 0)
            documents.erase(it);
        return result;
    }
    return error("SESSION_EXPIRED", "The session handle is unknown or its document has closed.");
}

bool NativeWindow::start_mcp_gateway() {
#ifdef _WIN32
    if (!mcp_runtime_) mcp_runtime_ = std::make_unique<NativeWindowAutomationRuntime>();
    mcp_error_text_.clear();
    if (!mcp_runtime_->start(*this, mcp_edit_access_, mcp_error_text_)) {
        std::cerr << "PikDraw MCP gateway could not start: " << mcp_error_text_ << '\n';
        mcp_runtime_.reset();
        return false;
    }
    mcp_error_text_.clear();
    return true;
#else
    mcp_error_text_ = "PikDraw MCP gateway is available only on Windows.";
    std::cerr << "PikDraw MCP gateway is available only on Windows.\n";
    return false;
#endif
}

bool NativeWindow::set_mcp_access(bool enabled, bool edit_access) noexcept {
    if (!enabled) {
        mcp_runtime_.reset();
        mcp_enabled_ = false;
        mcp_edit_access_ = false;
        mcp_error_text_.clear();
        return true;
    }
    if (mcp_runtime_ && mcp_enabled_ && mcp_runtime_->http.active() &&
        mcp_edit_access_ == edit_access)
        return true;

    mcp_runtime_.reset();
    mcp_enabled_ = true;
    mcp_edit_access_ = edit_access;
#ifdef _WIN32
    if (!hwnd_) {
        mcp_error_text_ = "The PikDraw window must be initialized before enabling MCP.";
        mcp_enabled_ = false;
        mcp_edit_access_ = false;
        return false;
    }
#endif
    try {
        if (start_mcp_gateway()) return true;
    } catch (const std::exception& error) {
        mcp_error_text_ = error.what();
    } catch (...) {
        mcp_error_text_ = "The MCP listener could not be started.";
    }
    if (!mcp_error_text_.empty()) {
        mcp_enabled_ = false;
        mcp_edit_access_ = false;
        return false;
    }
    mcp_error_text_ = "The MCP listener could not be started.";
    mcp_enabled_ = false;
    mcp_edit_access_ = false;
    return false;
}

bool NativeWindow::mcp_listener_active() const noexcept {
    return mcp_enabled_ && mcp_runtime_ != nullptr && mcp_runtime_->http.active();
}

bool NativeWindow::mcp_edit_access_active() const noexcept {
    return mcp_listener_active() && mcp_edit_access_;
}

const std::string& NativeWindow::mcp_error_message() const noexcept {
    return mcp_error_text_;
}

} // namespace pikdraw::desktop
