#include "pikdraw/desktop/shell/native_window.hpp"
#include "pikdraw/desktop/workflows/cut_geometry_job.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
using namespace pikdraw;
static void check(bool ok, const char* label) {
    if (!ok) { std::cerr << "[FAIL] " << label << '\n'; std::exit(1); }
}
int main() {
    desktop::NativeWindow window(1200,800);
#ifdef _WIN32
    check(window.initialize("Shape Builder geometry check"),"initialize");
#endif
    window.workflow().new_document();
    auto a=window.workflow().add_rectangle(50,50,100,100);
    auto b=window.workflow().add_rectangle(100,50,100,100);
    check(a && b,"create sources");
    desktop::CutGeometryRequest request;
    request.operation=desktop::CutGeometryOperation::ShapeBuilder;
    request.snapshot=window.root().document().current_snapshot();
    request.selected_ids={a,b};
    request.points_world={{75,100},{125,100}};
    auto merge=desktop::compute_shape_builder_geometry(request,{});
    check(merge.status==desktop::CutGeometryStatus::Success && merge.additions.size()==1,
          "two adjacent selected faces compute a single output");
    const auto merged=merge.additions.front().object.path.bounds();
    check(std::abs(merged.x-50)<0.1 && std::abs(merged.width-100)<0.1,
          "merged face bounds cover both selected faces");
    check(window.workflow().apply_cut_geometry_proposal(std::move(merge)),"apply merge");
    auto snapshot=window.root().document().current_snapshot();
    check(snapshot->object_count()==2 && !snapshot->objects().get(a) && snapshot->objects().get(b),
          "merge consumes the selected parts and preserves the other source remainder");
    const auto remainder=window.workflow().get_object_bounds(b);
    check(remainder && std::abs(remainder->x-150)<0.1 && std::abs(remainder->width-50)<0.1,
          "merge preserves only the unselected portion of the second source");
    check(window.workflow().undo() && window.root().document().current_snapshot()->object_count()==2,
          "undo removes merged output");
    request.snapshot=window.root().document().current_snapshot();
    request.points_world={{125,100}};
    request.erase_faces=true;
    auto erase=desktop::compute_shape_builder_geometry(request,{});
    check(erase.status==desktop::CutGeometryStatus::Success && erase.replacements.size()==2,
          "erase computes replacements for both sources");
    check(window.workflow().apply_cut_geometry_proposal(std::move(erase)),"apply erase");
    const auto left=window.workflow().get_object_bounds(a);
    const auto right=window.workflow().get_object_bounds(b);
    check(left && right && std::abs(left->width-50)<0.1 && std::abs(right->width-50)<0.1,
          "erase removes overlap from both sources");
    check(window.workflow().undo(),"undo erase");
    const auto restored=window.workflow().get_object_bounds(a);
    check(restored && std::abs(restored->width-100)<0.1,"undo restores source geometry");
    request.snapshot=window.root().document().current_snapshot();
    request.points_world={{75,100},{125,100}};
    request.erase_faces=false;
    request.shape_builder_gesture=true;
    auto drag_merge=desktop::compute_shape_builder_geometry(request,{});
    check(drag_merge.status==desktop::CutGeometryStatus::Success && drag_merge.additions.size()==1 &&
          std::abs(drag_merge.additions.front().object.path.bounds().width-100)<0.1,
          "gesture resolves each face crossed between pointer samples");
    request.points_world={{125,100}};
    request.erase_faces=true;
    auto alt_erase=desktop::compute_shape_builder_geometry(request,{});
    check(alt_erase.status==desktop::CutGeometryStatus::Success && alt_erase.replacements.size()==2,
          "erase gesture resolves overlap from both sources");
#ifdef _WIN32
    ShowWindow(window.hwnd(),SW_SHOW);
    window.fit_page_to_canvas(); window.set_active_tool(22);
    window.root().vector_controller().set_selection({a,b},*window.root().document().current_snapshot());
    RECT client{}; check(GetClientRect(window.hwnd(),&client)!=FALSE,"canvas bounds");
    const RECT canvas=window.canvas_rect(client.right,client.bottom);
    auto coordinates=[&](double x,double y) {
        const auto screen=window.root().renderer().camera().world_to_screen({x,y});
        return POINT{canvas.left+static_cast<int>(std::lround(screen.x)),
                     canvas.top+static_cast<int>(std::lround(screen.y))};
    };
    const auto start=coordinates(75,100), end=coordinates(125,100);
    SendMessageW(window.hwnd(),WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(start.x,start.y));
    SendMessageW(window.hwnd(),WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(end.x,end.y));
    SendMessageW(window.hwnd(),WM_LBUTTONUP,0,MAKELPARAM(end.x,end.y));
    check(window.cut_geometry_job_active(),"native drag starts worker");
    const auto deadline=GetTickCount64()+5000;
    while (window.cut_geometry_job_active() && GetTickCount64()<deadline) {
        MSG message{};
        if (PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {
            TranslateMessage(&message); DispatchMessageW(&message);
        } else (void)MsgWaitForMultipleObjects(0,nullptr,FALSE,5,QS_ALLINPUT);
    }
    check(!window.cut_geometry_job_active(),"native drag worker finishes");
    const auto after_drag=window.root().document().current_snapshot();
    check(after_drag->object_count()==2 && !after_drag->objects().get(a) && after_drag->objects().get(b),
          "native drag commits merged area and remainder without duplicating sources");
    check(window.workflow().undo() && window.root().document().current_snapshot()->object_count()==2,
          "one Undo restores both sources after native drag");
    window.root().vector_controller().set_selection({a,b},*window.root().document().current_snapshot());
    const auto overlap=coordinates(125,100);
    BYTE old_keys[256]{};
    check(GetKeyboardState(old_keys)!=FALSE,"read keyboard state");
    BYTE alt_keys[256]{};
    std::copy(std::begin(old_keys),std::end(old_keys),std::begin(alt_keys));
    alt_keys[VK_MENU] |= 0x80;
    check(SetKeyboardState(alt_keys)!=FALSE,"set Alt state for native test");
    SendMessageW(window.hwnd(),WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(overlap.x,overlap.y));
    SendMessageW(window.hwnd(),WM_LBUTTONUP,0,MAKELPARAM(overlap.x,overlap.y));
    (void)SetKeyboardState(old_keys);
    check(window.cut_geometry_job_active(),"native Alt click starts erase worker");
    const auto erase_deadline=GetTickCount64()+5000;
    while (window.cut_geometry_job_active() && GetTickCount64()<erase_deadline) {
        MSG message{};
        if (PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {
            TranslateMessage(&message); DispatchMessageW(&message);
        } else (void)MsgWaitForMultipleObjects(0,nullptr,FALSE,5,QS_ALLINPUT);
    }
    const auto erased_left=window.workflow().get_object_bounds(a);
    const auto erased_right=window.workflow().get_object_bounds(b);
    check(!window.cut_geometry_job_active() && erased_left && erased_right &&
          std::abs(erased_left->width-50)<0.2 && std::abs(erased_right->width-50)<0.2,
          "native Alt click erases overlap from both sources");
    check(window.workflow().undo(),"native Alt erase is one Undo");
#endif
}
