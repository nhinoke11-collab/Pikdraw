#include "pikdraw/desktop/shell/native_window.hpp"
#include "pikdraw/document/commands/command.hpp"
#include <cmath>
#include <cstdlib>
#include <iostream>
using pikdraw::desktop::NativeWindow;
static void check(bool ok, const char* message) {
    if (!ok) { std::cerr << "[FAIL] " << message << '\n'; std::exit(EXIT_FAILURE); }
}
#ifdef _WIN32
static bool wait_for_job(NativeWindow& window) {
    const auto deadline=GetTickCount64()+5000;
    while (window.cut_geometry_job_active() && GetTickCount64()<deadline) {
        MSG message{};
        if (PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {
            TranslateMessage(&message); DispatchMessageW(&message);
        } else (void)MsgWaitForMultipleObjects(0,nullptr,FALSE,5,QS_ALLINPUT);
    }
    return !window.cut_geometry_job_active();
}
#endif
int main() {
#ifdef _WIN32
    NativeWindow window(1200,800);
    check(window.initialize("Smart Fill repeated click regression"),"native window initializes");
    ShowWindow(window.hwnd(),SW_SHOW);
    window.workflow().new_document();
    const auto a=window.workflow().add_rectangle(50,50,100,100);
    const auto b=window.workflow().add_rectangle(100,50,100,100);
    check(a && b,"source rectangles created");
    window.fit_page_to_canvas(); window.set_active_tool(23);
    RECT client{}; check(GetClientRect(window.hwnd(),&client)!=FALSE,"client bounds");
    const RECT canvas=window.canvas_rect(client.right,client.bottom);
    auto click=[&](double x,double y) {
        const auto pos=window.root().renderer().camera().world_to_screen({x,y});
        const int sx=canvas.left+static_cast<int>(std::lround(pos.x));
        const int sy=canvas.top+static_cast<int>(std::lround(pos.y));
        SendMessageW(window.hwnd(),WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(sx,sy));
        SendMessageW(window.hwnd(),WM_LBUTTONUP,0,MAKELPARAM(sx,sy));
        check(window.cut_geometry_job_active(),"click starts Smart Fill worker");
        check(wait_for_job(window),"worker completes");
    };
    window.root().vector_controller().selection().clear();
    click(125,100);
    check(window.root().document().current_snapshot()->object_count()==3,"first click creates face");
    auto selected=window.root().vector_controller().selection().selected_ids();
    check(selected.size()==2 && window.root().vector_controller().selection().is_selected(a) &&
          window.root().vector_controller().selection().is_selected(b),"source scope remains selected");
    click(75,100);
    check(window.root().document().current_snapshot()->object_count()==4,"second click creates another face");
    click(175,100);
    check(window.root().document().current_snapshot()->object_count()==5,"third click creates another face");
    check(window.workflow().undo() && window.root().document().current_snapshot()->object_count()==4,
          "one Undo removes only third face");
    click(175,100);
    check(window.root().document().current_snapshot()->object_count()==5,"click after Undo creates face");
    window.workflow().new_document();
    auto add_line=[&](double x1,double y1,double x2,double y2) {
        pikdraw::document::PathObject path;
        path.id=pikdraw::document::generate_object_id(); path.has_fill=false; path.has_stroke=true;
        path.path.move_to(x1,y1); path.path.line_to(x2,y2);
        const auto result=window.root().document().execute_command(
            std::make_unique<pikdraw::document::AddObjectCommand>(std::move(path),0));
        check(result.success,"open line created");
    };
    add_line(50,50,150,50); add_line(150,50,150,150);
    add_line(150,150,50,150); add_line(50,150,50,50); add_line(100,40,100,160);
    window.fit_page_to_canvas(); window.set_active_tool(23);
    window.root().vector_controller().selection().clear();
    click(75,100);
    check(window.root().document().current_snapshot()->object_count()==6,"first open-line face created");
    click(125,100);
    check(window.root().document().current_snapshot()->object_count()==7,"second open-line face created");
    check(window.workflow().undo() && window.root().document().current_snapshot()->object_count()==6,
          "open-line Undo removes only the last face");
#endif
}
