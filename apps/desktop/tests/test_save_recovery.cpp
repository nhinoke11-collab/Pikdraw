#include "pikdraw/desktop/shell/native_window.hpp"
#include "pikdraw/document/storage/save_recovery.hpp"
#include "../src/shell/native_window_menus.hpp"
#include <cassert>
#include <filesystem>
#include <iostream>

using namespace pikdraw;

#ifdef _WIN32
struct TemporaryLocalData {
    std::filesystem::path root;
    std::wstring previous;
    TemporaryLocalData() {
        const auto size = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
        previous.resize(size ? size : 1);
        const auto copied = GetEnvironmentVariableW(L"LOCALAPPDATA", previous.data(),
                                                     static_cast<DWORD>(previous.size()));
        previous.resize(copied);
        root = std::filesystem::temp_directory_path() /
            (L"pikdraw-autosave-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
             std::to_wstring(GetTickCount64()));
        assert(!std::filesystem::exists(root));
        std::filesystem::create_directory(root);
        assert(SetEnvironmentVariableW(L"LOCALAPPDATA", root.c_str()));
    }
    ~TemporaryLocalData() {
        SetEnvironmentVariableW(L"LOCALAPPDATA", previous.empty() ? nullptr : previous.c_str());
        // Delete only the explicitly created test directory under the OS temp root.
        assert(root.is_absolute() && root.parent_path() == std::filesystem::temp_directory_path());
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
    }
};

size_t checkpoint_count(const std::filesystem::path& root) {
    if (!std::filesystem::exists(root)) return 0;
    size_t count = 0;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root))
        if (entry.path().extension() == ".pkdr") ++count;
    return count;
}

std::string utf8_path(const std::filesystem::path& path) {
    const auto bytes = path.u8string();
    return {bytes.begin(), bytes.end()};
}
#endif

int main() {
#ifdef _WIN32
    TemporaryLocalData temporary;
    desktop::NativeWindow window;
    assert(window.initialize("PikDraw auto-recovery regression"));
    const auto tick = [&] {
        SendMessageW(window.hwnd(), WM_TIMER, desktop::NativeWindow::kAutoSaveTimerId, 0);
    };
    const auto recovery = temporary.root / "PikDraw" / "Recovery";
    tick(); assert(checkpoint_count(recovery) == 0);
    window.workflow().set_page_size(100, 150);
    const auto first_epoch = window.root().document().epoch();
    const auto first_revision = window.root().document().revision();
    window.create_new_tab("Second", 200, 150);
    window.workflow().set_page_size(220, 150);
    // Both tabs intentionally share the default document ID; their recovery paths must differ.
    assert(window.tabs()[0].root->document().id() == window.root().document().id());
    tick(); assert(checkpoint_count(recovery) == 2);
    const auto& first = window.tabs()[0];
    const auto& second = window.tabs()[1];
    assert(first.recovery_directory != second.recovery_directory);
    auto first_checkpoint = document::SaveRecoveryService::find_latest_checkpoint(
        first.recovery_directory, first.root->document().id());
    assert(first_checkpoint && first_checkpoint->epoch == first_epoch &&
           first_checkpoint->revision == first_revision);
    const auto snapshot = document::SaveRecoveryService::load(first_checkpoint->path);
    assert(snapshot && snapshot->page().width_mm == 100);
    assert(first.root->document().is_dirty() && first.root->document().can_undo());
    tick(); assert(checkpoint_count(recovery) == 2); // No duplicate revision.

    bool truncated = false;
    const auto candidates = document::SaveRecoveryService::list_recovery_checkpoints(
        utf8_path(recovery), &truncated);
    assert(candidates.size() == 2 && !truncated);
    assert(candidates.front().document_id == first.root->document().id());
    assert(candidates.front().object_count == 0);
    assert(candidates.front().page_width_mm > 0.0 && !candidates.front().timestamp.empty());
    {
        desktop::NativeWindow recovery_window;
        assert(recovery_window.initialize("PikDraw recovery UI regression"));
        assert(recovery_window.is_recovery_modal_open());
        assert(recovery_window.recovery_candidate_count() == 2);
        SendMessageW(recovery_window.hwnd(), WM_KEYDOWN, VK_RETURN, 0);
        assert(!recovery_window.is_recovery_modal_open());
        assert(recovery_window.tab_count() == 2);
        assert(recovery_window.root().document().is_dirty());
        assert(recovery_window.active_tab().file_path.empty());
        assert(recovery_window.active_tab().recovery_written);
        assert(recovery_window.root().document().id() == first.root->document().id());
    }
    {
        desktop::NativeWindow later_window;
        assert(later_window.initialize("PikDraw recovery later regression"));
        assert(later_window.is_recovery_modal_open());
        SendMessageW(later_window.hwnd(), WM_KEYDOWN, VK_ESCAPE, 0);
        assert(!later_window.is_recovery_modal_open());
        assert(checkpoint_count(recovery) == 2);
    }
    window.workflow().set_page_size(230, 150);
    tick(); assert(checkpoint_count(recovery) == 2); // Atomic replacement/rotation.
    window.active_tab().file_path = utf8_path(temporary.root / "missing" / "failed.pikdraw");
    window.execute_menu_action(static_cast<int>(desktop::MenuActionId::FileSave));
    assert(window.root().document().is_dirty() && checkpoint_count(recovery) == 2);
    window.active_tab().file_path = utf8_path(temporary.root / "second.pikdraw");
    window.execute_menu_action(static_cast<int>(desktop::MenuActionId::FileSave));
    assert(!window.root().document().is_dirty() && checkpoint_count(recovery) == 1);
    tick(); assert(checkpoint_count(recovery) == 1);
    window.switch_to_tab(0);
    assert(window.root().document().undo());
    tick(); assert(checkpoint_count(recovery) == 1); // Undo retains one bounded checkpoint.
    assert(window.root().document().redo());
    window.active_tab().file_path = utf8_path(temporary.root / "first.pikdraw");
    window.execute_menu_action(static_cast<int>(desktop::MenuActionId::FileSave));
    assert(checkpoint_count(recovery) == 0);
    window.workflow().set_page_size(120, 160);
    tick();
    const auto listed = document::SaveRecoveryService::list_recovery_checkpoints(
        utf8_path(recovery));
    assert(listed.size() == 1);
    std::string discard_error;
    assert(!document::SaveRecoveryService::discard_recovery_checkpoint(
        utf8_path(recovery), utf8_path(temporary.root / "outside.pkdr"), &discard_error));
    assert(!discard_error.empty() && checkpoint_count(recovery) == 1);
    assert(document::SaveRecoveryService::discard_recovery_checkpoint(
        utf8_path(recovery), listed.front().path));
    assert(checkpoint_count(recovery) == 0);
    window.execute_menu_action(static_cast<int>(desktop::MenuActionId::FileRecoverAutosave));
    assert(window.is_recovery_modal_open() && window.recovery_candidate_count() == 0);
    assert(window.root().window().action_dispatcher().last_dispatched_id() ==
           "action.file.recover_autosave");
    SendMessageW(window.hwnd(), WM_KEYDOWN, VK_ESCAPE, 0);
    assert(!window.is_recovery_modal_open());
    std::cout << "PASS native timer per-tab autosave and successful-save cleanup\n";
#endif
}
