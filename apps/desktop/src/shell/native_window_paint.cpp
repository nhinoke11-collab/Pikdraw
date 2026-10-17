#include "pikdraw/desktop/shell/native_window.hpp"
#include "native_window_file_menu.hpp"
#include "native_window_menus.hpp"
#include "native_window_page_presets.hpp"
#include "../tools/drawing_tool_controller.hpp"
#include "../tools/pen_tool_controller.hpp"
#include "../tools/node_tool_controller.hpp"
#include "../tools/contour_tool_controller.hpp"

#include <iostream>
#include <fstream>
#include <vector>
#include <iomanip>
#include <sstream>
#include <cmath>
#include <numbers>
#include <type_traits>
#include <variant>

namespace pikdraw::desktop {

#ifdef _WIN32
namespace {

struct GpPointF {
    float X;
    float Y;
};

using GpStatus = int;
using GpGraphics = void*;
using GpPen = void*;
using GpBrush = void*;
using GpSolidFill = void*;
using GpPath = void*;

constexpr int kSmoothingModeAntiAlias = 4;
constexpr int kPixelOffsetModeHalf = 4;
constexpr int kLineCapRound = 2;
constexpr int kLineJoinRound = 2;
constexpr int kUnitPixel = 2;
constexpr int kFillModeAlternate = 0;

struct GdiplusStartupInput {
    UINT32 GdiplusVersion{1};
    void* DebugEventCallback{nullptr};
    BOOL SuppressBackgroundThread{FALSE};
    BOOL SuppressExternalCodecs{FALSE};
};

using FnGdiplusStartup = GpStatus(WINAPI*)(ULONG_PTR*, const GdiplusStartupInput*, void*);
using FnGdiplusShutdown = void(WINAPI*)(ULONG_PTR);
using FnGdipCreateFromHDC = GpStatus(WINAPI*)(HDC, GpGraphics**);
using FnGdipDeleteGraphics = GpStatus(WINAPI*)(GpGraphics*);
using FnGdipSetSmoothingMode = GpStatus(WINAPI*)(GpGraphics*, int);
using FnGdipSetPixelOffsetMode = GpStatus(WINAPI*)(GpGraphics*, int);
using FnGdipCreatePen1 = GpStatus(WINAPI*)(DWORD, float, int, GpPen**);
using FnGdipDeletePen = GpStatus(WINAPI*)(GpPen*);
using FnGdipSetPenLineCap197819 = GpStatus(WINAPI*)(GpPen*, int, int, int);
using FnGdipSetPenLineJoin = GpStatus(WINAPI*)(GpPen*, int);
using FnGdipDrawLine = GpStatus(WINAPI*)(GpGraphics*, GpPen*, float, float, float, float);
using FnGdipDrawLines = GpStatus(WINAPI*)(GpGraphics*, GpPen*, const GpPointF*, int);
using FnGdipDrawEllipse = GpStatus(WINAPI*)(GpGraphics*, GpPen*, float, float, float, float);
using FnGdipFillEllipse = GpStatus(WINAPI*)(GpGraphics*, GpBrush*, float, float, float, float);
using FnGdipDrawRectangle = GpStatus(WINAPI*)(GpGraphics*, GpPen*, float, float, float, float);
using FnGdipFillRectangle = GpStatus(WINAPI*)(GpGraphics*, GpBrush*, float, float, float, float);
using FnGdipCreateSolidFill = GpStatus(WINAPI*)(DWORD, GpSolidFill**);
using FnGdipDeleteBrush = GpStatus(WINAPI*)(GpBrush*);
using FnGdipCreatePath = GpStatus(WINAPI*)(int, GpPath**);
using FnGdipDeletePath = GpStatus(WINAPI*)(GpPath*);
using FnGdipAddPathLine = GpStatus(WINAPI*)(GpPath*, float, float, float, float);
using FnGdipAddPathArc = GpStatus(WINAPI*)(GpPath*, float, float, float, float, float, float);
using FnGdipClosePathFigure = GpStatus(WINAPI*)(GpPath*);
using FnGdipDrawPath = GpStatus(WINAPI*)(GpGraphics*, GpPen*, GpPath*);
using FnGdipFillPath = GpStatus(WINAPI*)(GpGraphics*, GpBrush*, GpPath*);
using FnGdipFillPolygon = GpStatus(WINAPI*)(GpGraphics*, GpBrush*, const GpPointF*, int, int);

struct GdiplusApi {
    HMODULE hModule{nullptr};
    ULONG_PTR token{0};
    bool valid{false};

    FnGdiplusStartup Startup{nullptr};
    FnGdiplusShutdown Shutdown{nullptr};
    FnGdipCreateFromHDC CreateFromHDC{nullptr};
    FnGdipDeleteGraphics DeleteGraphics{nullptr};
    FnGdipSetSmoothingMode SetSmoothingMode{nullptr};
    FnGdipSetPixelOffsetMode SetPixelOffsetMode{nullptr};
    FnGdipCreatePen1 CreatePen1{nullptr};
    FnGdipDeletePen DeletePen{nullptr};
    FnGdipSetPenLineCap197819 SetPenLineCap197819{nullptr};
    FnGdipSetPenLineJoin SetPenLineJoin{nullptr};
    FnGdipDrawLine DrawLine{nullptr};
    FnGdipDrawLines DrawLines{nullptr};
    FnGdipDrawEllipse DrawEllipse{nullptr};
    FnGdipFillEllipse FillEllipse{nullptr};
    FnGdipDrawRectangle DrawRectangle{nullptr};
    FnGdipFillRectangle FillRectangle{nullptr};
    FnGdipCreateSolidFill CreateSolidFill{nullptr};
    FnGdipDeleteBrush DeleteBrush{nullptr};
    FnGdipCreatePath CreatePath{nullptr};
    FnGdipDeletePath DeletePath{nullptr};
    FnGdipAddPathLine AddPathLine{nullptr};
    FnGdipAddPathArc AddPathArc{nullptr};
    FnGdipClosePathFigure ClosePathFigure{nullptr};
    FnGdipDrawPath DrawPath{nullptr};
    FnGdipFillPath FillPath{nullptr};
    FnGdipFillPolygon FillPolygon{nullptr};

    static GdiplusApi* get() {
        static GdiplusApi instance;
        return instance.valid ? &instance : nullptr;
    }

private:
    GdiplusApi() {
        hModule = LoadLibraryW(L"gdiplus.dll");
        if (!hModule) return;

        #define LOAD_PROC(member, name) \
            member = reinterpret_cast<Fn##name>(GetProcAddress(hModule, #name)); \
            if (!member) return

        LOAD_PROC(Startup, GdiplusStartup);
        LOAD_PROC(Shutdown, GdiplusShutdown);
        LOAD_PROC(CreateFromHDC, GdipCreateFromHDC);
        LOAD_PROC(DeleteGraphics, GdipDeleteGraphics);
        LOAD_PROC(SetSmoothingMode, GdipSetSmoothingMode);
        LOAD_PROC(SetPixelOffsetMode, GdipSetPixelOffsetMode);
        LOAD_PROC(CreatePen1, GdipCreatePen1);
        LOAD_PROC(DeletePen, GdipDeletePen);
        LOAD_PROC(SetPenLineCap197819, GdipSetPenLineCap197819);
        LOAD_PROC(SetPenLineJoin, GdipSetPenLineJoin);
        LOAD_PROC(DrawLine, GdipDrawLine);
        LOAD_PROC(DrawLines, GdipDrawLines);
        LOAD_PROC(DrawEllipse, GdipDrawEllipse);
        LOAD_PROC(FillEllipse, GdipFillEllipse);
        LOAD_PROC(DrawRectangle, GdipDrawRectangle);
        LOAD_PROC(FillRectangle, GdipFillRectangle);
        LOAD_PROC(CreateSolidFill, GdipCreateSolidFill);
        LOAD_PROC(DeleteBrush, GdipDeleteBrush);
        LOAD_PROC(CreatePath, GdipCreatePath);
        LOAD_PROC(DeletePath, GdipDeletePath);
        LOAD_PROC(AddPathLine, GdipAddPathLine);
        LOAD_PROC(AddPathArc, GdipAddPathArc);
        LOAD_PROC(ClosePathFigure, GdipClosePathFigure);
        LOAD_PROC(DrawPath, GdipDrawPath);
        LOAD_PROC(FillPath, GdipFillPath);
        LOAD_PROC(FillPolygon, GdipFillPolygon);

        #undef LOAD_PROC

        GdiplusStartupInput input{};
        if (Startup(&token, &input, nullptr) == 0) {
            valid = true;
        }
    }

    ~GdiplusApi() {
        if (valid && Shutdown) {
            Shutdown(token);
        }
        if (hModule) {
            FreeLibrary(hModule);
        }
    }
};

inline DWORD MakeGdiplusARGB(COLORREF c, BYTE alpha = 255) {
    return (static_cast<DWORD>(alpha) << 24) |
           (static_cast<DWORD>(GetRValue(c)) << 16) |
           (static_cast<DWORD>(GetGValue(c)) << 8) |
           static_cast<DWORD>(GetBValue(c));
}

void draw_toolbox_flyout_marker(HDC dc, int right_x, int bottom_y, COLORREF color) {
    auto* api = GdiplusApi::get();
    if (api) {
        GpGraphics* g = nullptr;
        if (api->CreateFromHDC(dc, &g) == 0 && g) {
            api->SetSmoothingMode(g, kSmoothingModeAntiAlias);
            const float rx = static_cast<float>(right_x);
            const float by = static_cast<float>(bottom_y);
            const float size = 4.0f;
            GpPointF pts[3] = {
                {rx, by - size},
                {rx, by},
                {rx - size, by}
            };
            GpSolidFill* brush = nullptr;
            api->CreateSolidFill(MakeGdiplusARGB(color, 210), &brush);
            api->FillPolygon(g, brush, pts, 3, kFillModeAlternate);
            api->DeleteBrush(brush);
            api->DeleteGraphics(g);
            return;
        }
    }
    HPEN pen = CreatePen(PS_SOLID, 1, color);
    HBRUSH brush = CreateSolidBrush(color);
    const HGDIOBJ old_pen = SelectObject(dc, pen);
    const HGDIOBJ old_brush = SelectObject(dc, brush);
    POINT gdi_pts[3] = {
        {right_x, bottom_y - 4},
        {right_x, bottom_y},
        {right_x - 4, bottom_y}
    };
    Polygon(dc, gdi_pts, 3);
    SelectObject(dc, old_brush);
    SelectObject(dc, old_pen);
    DeleteObject(brush);
    DeleteObject(pen);
}

void draw_tool_icon_gdiplus(GdiplusApi* api, GpGraphics* g, int tool, float cx, float cy, COLORREF color) {
    GpPen* pen = nullptr;
    api->CreatePen1(MakeGdiplusARGB(color, 255), 1.55f, kUnitPixel, &pen);
    api->SetPenLineCap197819(pen, kLineCapRound, kLineCapRound, 2);
    api->SetPenLineJoin(pen, kLineJoinRound);

    GpSolidFill* fill_brush = nullptr;
    api->CreateSolidFill(MakeGdiplusARGB(color, 255), &fill_brush);

    if (tool == 0) {
        // Tool 0: Modern 45° vector pointer arrow (Figma / Illustrator 2026 style)
        GpPointF pts[] = {
            {cx - 5.5f, cy - 8.5f},
            {cx - 5.5f, cy + 5.5f},
            {cx - 1.8f, cy + 2.0f},
            {cx + 2.0f, cy + 8.5f},
            {cx + 4.2f, cy + 7.2f},
            {cx + 0.8f, cy + 1.2f},
            {cx + 6.2f, cy + 1.2f}
        };
        GpPath* path = nullptr;
        api->CreatePath(kFillModeAlternate, &path);
        for (int i = 0; i < 7; ++i) {
            int next = (i + 1) % 7;
            api->AddPathLine(path, pts[i].X, pts[i].Y, pts[next].X, pts[next].Y);
        }
        api->ClosePathFigure(path);
        api->DrawPath(g, pen, path);
        api->DeletePath(path);
    } else if (tool == 1) {
        // Tool 1: Rounded rectangle with subtle 2.2px corner radius
        const float w = 15.0f, h = 11.5f, r = 2.2f;
        const float x = cx - w * 0.5f, y = cy - h * 0.5f;
        GpPath* path = nullptr;
        api->CreatePath(kFillModeAlternate, &path);
        api->AddPathArc(path, x + w - 2 * r, y, 2 * r, 2 * r, 270.0f, 90.0f);
        api->AddPathArc(path, x + w - 2 * r, y + h - 2 * r, 2 * r, 2 * r, 0.0f, 90.0f);
        api->AddPathArc(path, x, y + h - 2 * r, 2 * r, 2 * r, 90.0f, 90.0f);
        api->AddPathArc(path, x, y, 2 * r, 2 * r, 180.0f, 90.0f);
        api->ClosePathFigure(path);
        api->DrawPath(g, pen, path);
        api->DeletePath(path);
    } else if (tool == 2) {
        // Tool 2: Precision anti-aliased circle/ellipse
        const float d = 14.0f;
        api->DrawEllipse(g, pen, cx - d * 0.5f, cy - d * 0.5f, d, d);
    } else if (tool == 3) {
        // Tool 3: Modern ergonomic vector open hand (Pan tool)
        GpPointF hand_pts[] = {
            {cx - 3.2f, cy + 8.0f},
            {cx - 4.5f, cy + 4.5f},
            {cx - 7.5f, cy + 2.2f},
            {cx - 7.2f, cy + 0.2f},
            {cx - 4.2f, cy + 0.5f},
            {cx - 3.2f, cy - 6.5f},
            {cx - 2.0f, cy - 7.8f},
            {cx - 1.0f, cy - 4.0f},
            {cx - 0.2f, cy - 8.5f},
            {cx + 0.8f, cy - 8.5f},
            {cx + 1.8f, cy - 4.2f},
            {cx + 2.8f, cy - 7.6f},
            {cx + 3.8f, cy - 7.4f},
            {cx + 4.5f, cy - 3.0f},
            {cx + 5.8f, cy - 5.0f},
            {cx + 6.8f, cy - 4.2f},
            {cx + 5.2f, cy + 4.0f},
            {cx + 3.8f, cy + 8.0f}
        };
        GpPath* path = nullptr;
        api->CreatePath(kFillModeAlternate, &path);
        for (size_t i = 0; i < sizeof(hand_pts) / sizeof(hand_pts[0]); ++i) {
            size_t next = (i + 1) % (sizeof(hand_pts) / sizeof(hand_pts[0]));
            api->AddPathLine(path, hand_pts[i].X, hand_pts[i].Y, hand_pts[next].X, hand_pts[next].Y);
        }
        api->ClosePathFigure(path);
        api->DrawPath(g, pen, path);
        api->DeletePath(path);
    } else if (tool == 4) {
        // Tool 4: Typographic Text tool: balanced, crisp 'T' with optical serifs
        api->DrawLine(g, pen, cx - 6.5f, cy - 6.5f, cx + 6.5f, cy - 6.5f);
        api->DrawLine(g, pen, cx - 6.5f, cy - 6.5f, cx - 6.5f, cy - 4.2f);
        api->DrawLine(g, pen, cx + 6.5f, cy - 6.5f, cx + 6.5f, cy - 4.2f);
        api->DrawLine(g, pen, cx, cy - 6.5f, cx, cy + 7.5f);
        api->DrawLine(g, pen, cx - 3.2f, cy + 7.5f, cx + 3.2f, cy + 7.5f);
    } else if (tool == 5) {
        // Tool 5: Vector knife / scalpel with precision angled blade and guide line
        api->DrawLine(g, pen, cx - 6.5f, cy + 6.5f, cx - 2.0f, cy + 6.5f);
        api->DrawLine(g, pen, cx - 2.0f, cy + 6.5f, cx + 2.0f, cy + 2.5f);
        api->DrawLine(g, pen, cx + 2.0f, cy + 2.5f, cx + 6.5f, cy - 2.0f);
        api->DrawLine(g, pen, cx + 6.5f, cy - 2.0f, cx + 4.5f, cy - 4.0f);
        api->DrawLine(g, pen, cx + 4.5f, cy - 4.0f, cx + 0.0f, cy + 0.5f);
        api->DrawLine(g, pen, cx + 0.0f, cy + 0.5f, cx - 6.5f, cy + 6.5f);
        api->DrawLine(g, pen, cx - 3.5f, cy + 3.5f, cx - 2.0f, cy + 6.5f);
        GpPen* cut_pen = nullptr;
        api->CreatePen1(MakeGdiplusARGB(color, 140), 1.0f, kUnitPixel, &cut_pen);
        api->DrawLine(g, cut_pen, cx - 8.5f, cy + 8.5f, cx + 7.5f, cy - 7.5f);
        api->DeletePen(cut_pen);
    } else if (tool == 6) {
        // Tool 6: Precision 45° vector line with anchor point nodes
        const float x1 = cx - 6.5f, y1 = cy + 6.5f;
        const float x2 = cx + 6.5f, y2 = cy - 6.5f;
        api->DrawLine(g, pen, x1, y1, x2, y2);
        api->FillRectangle(g, fill_brush, x1 - 1.8f, y1 - 1.8f, 3.6f, 3.6f);
        api->FillRectangle(g, fill_brush, x2 - 1.8f, y2 - 1.8f, 3.6f, 3.6f);
    } else if (tool == 7) {
        // Tool 7: Polyline path with 3 vector anchor nodes
        GpPointF poly_pts[3] = {
            {cx - 7.5f, cy + 4.5f},
            {cx - 1.0f, cy - 6.0f},
            {cx + 7.5f, cy + 2.5f}
        };
        api->DrawLines(g, pen, poly_pts, 3);
        for (int i = 0; i < 3; ++i) {
            api->FillEllipse(g, fill_brush, poly_pts[i].X - 1.8f, poly_pts[i].Y - 1.8f, 3.6f, 3.6f);
        }
    } else if (tool == 8) {
        // Tool 8: Canonical Gold-Standard Fountain Pen Nib (Ngòi bút máy vector)
        const float tip_x = cx - 7.2f, tip_y = cy + 7.2f;
        const float hole_x = cx - 1.0f, hole_y = cy + 1.0f;
        GpPointF nib_pts[] = {
            {tip_x, tip_y},
            {cx - 6.2f, cy + 1.5f},
            {cx - 2.8f, cy - 2.2f},
            {cx + 0.8f, cy - 5.8f},
            {cx + 5.8f, cy - 0.8f},
            {cx + 2.2f, cy + 2.8f},
            {cx - 1.5f, cy + 6.2f}
        };
        GpPath* nib_path = nullptr;
        api->CreatePath(kFillModeAlternate, &nib_path);
        for (int i = 0; i < 7; ++i) {
            int next = (i + 1) % 7;
            api->AddPathLine(nib_path, nib_pts[i].X, nib_pts[i].Y, nib_pts[next].X, nib_pts[next].Y);
        }
        api->ClosePathFigure(nib_path);
        api->DrawPath(g, pen, nib_path);
        api->DeletePath(nib_path);

        api->FillEllipse(g, fill_brush, hole_x - 1.25f, hole_y - 1.25f, 2.5f, 2.5f);
        api->DrawLine(g, pen, tip_x, tip_y, hole_x, hole_y);
        api->DrawLine(g, pen, cx + 2.5f, cy - 7.5f, cx + 7.5f, cy - 2.5f);
    } else if (tool == 9) {
        // Tool 9: Regular Hexagon with rounded vertex joins
        const float R = 7.5f;
        GpPointF hex[6];
        for (int i = 0; i < 6; ++i) {
            const float a = -1.5707963f + static_cast<float>(i) * (3.14159265f / 3.0f);
            hex[i] = {cx + R * std::cos(a), cy + R * std::sin(a)};
        }
        GpPath* path = nullptr;
        api->CreatePath(kFillModeAlternate, &path);
        for (int i = 0; i < 6; ++i) {
            int next = (i + 1) % 6;
            api->AddPathLine(path, hex[i].X, hex[i].Y, hex[next].X, hex[next].Y);
        }
        api->ClosePathFigure(path);
        api->DrawPath(g, pen, path);
        api->DeletePath(path);
    } else if (tool == 10) {
        // Tool 10: Modern adjustment sliders (equalizer / appearance filters)
        api->DrawLine(g, pen, cx - 7.5f, cy - 5.5f, cx + 7.5f, cy - 5.5f);
        api->FillEllipse(g, fill_brush, cx - 3.8f, cy - 7.3f, 3.6f, 3.6f);
        api->DrawLine(g, pen, cx - 7.5f, cy, cx + 7.5f, cy);
        api->FillEllipse(g, fill_brush, cx + 1.2f, cy - 1.8f, 3.6f, 3.6f);
        api->DrawLine(g, pen, cx - 7.5f, cy + 5.5f, cx + 7.5f, cy + 5.5f);
        api->FillEllipse(g, fill_brush, cx - 5.0f, cy + 3.7f, 3.6f, 3.6f);
    } else if (tool == 11) {
        // Tool 11: Gaussian Blur: focal point with radiating soft diffusion rings
        api->FillEllipse(g, fill_brush, cx - 1.6f, cy - 1.6f, 3.2f, 3.2f);
        api->DrawEllipse(g, pen, cx - 4.8f, cy - 4.8f, 9.6f, 9.6f);
        GpPen* outer_pen = nullptr;
        api->CreatePen1(MakeGdiplusARGB(color, 160), 1.2f, kUnitPixel, &outer_pen);
        api->DrawEllipse(g, outer_pen, cx - 8.0f, cy - 8.0f, 16.0f, 16.0f);
        api->DeletePen(outer_pen);
    } else if (tool == 12) {
        // Tool 12: Drop Shadow: front object with offset soft elevation shadow
        GpPen* shadow_pen = nullptr;
        api->CreatePen1(MakeGdiplusARGB(color, 140), 1.2f, kUnitPixel, &shadow_pen);
        api->DrawRectangle(g, shadow_pen, cx - 2.0f, cy - 2.0f, 9.0f, 9.0f);
        api->DeletePen(shadow_pen);

        GpSolidFill* front_bg = nullptr;
        api->CreateSolidFill(MakeGdiplusARGB(RGB(20, 24, 33), 255), &front_bg);
        api->FillRectangle(g, front_bg, cx - 7.0f, cy - 7.0f, 9.0f, 9.0f);
        api->DeleteBrush(front_bg);
        api->DrawRectangle(g, pen, cx - 7.0f, cy - 7.0f, 9.0f, 9.0f);
    }

    api->DeleteBrush(fill_brush);
    api->DeletePen(pen);
}

void draw_tool_icon_gdi_fallback(HDC dc, int tool, int center_x, int center_y, COLORREF color) {
    HPEN pen = CreatePen(PS_SOLID, 1, color);
    const HGDIOBJ old_pen = SelectObject(dc, pen);
    const HGDIOBJ old_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    if (tool == 0) {
        MoveToEx(dc, center_x - 5, center_y - 8, nullptr);
        LineTo(dc, center_x - 5, center_y + 5);
        LineTo(dc, center_x - 1, center_y + 2);
        LineTo(dc, center_x + 2, center_y + 8);
        LineTo(dc, center_x + 4, center_y + 7);
        LineTo(dc, center_x + 1, center_y + 1);
        LineTo(dc, center_x + 6, center_y + 1);
        LineTo(dc, center_x - 5, center_y - 8);
    } else if (tool == 1) {
        Rectangle(dc, center_x - 7, center_y - 5, center_x + 8, center_y + 6);
    } else if (tool == 2) {
        Ellipse(dc, center_x - 7, center_y - 7, center_x + 7, center_y + 7);
    } else if (tool == 3) {
        MoveToEx(dc, center_x - 5, center_y + 7, nullptr);
        LineTo(dc, center_x - 7, center_y + 2);
        LineTo(dc, center_x - 5, center_y);
        LineTo(dc, center_x - 2, center_y - 6);
        LineTo(dc, center_x, center_y - 8);
        LineTo(dc, center_x + 2, center_y - 6);
        LineTo(dc, center_x + 4, center_y - 6);
        LineTo(dc, center_x + 6, center_y - 3);
        LineTo(dc, center_x + 5, center_y + 7);
        LineTo(dc, center_x - 5, center_y + 7);
    } else if (tool == 4) {
        MoveToEx(dc, center_x - 6, center_y - 6, nullptr);
        LineTo(dc, center_x + 6, center_y - 6);
        MoveToEx(dc, center_x, center_y - 6, nullptr);
        LineTo(dc, center_x, center_y + 7);
    } else if (tool == 5) {
        MoveToEx(dc, center_x - 6, center_y + 6, nullptr);
        LineTo(dc, center_x - 2, center_y + 6);
        LineTo(dc, center_x + 6, center_y - 2);
        LineTo(dc, center_x + 4, center_y - 4);
        LineTo(dc, center_x - 6, center_y + 6);
    } else if (tool == 6) {
        MoveToEx(dc, center_x - 6, center_y + 6, nullptr);
        LineTo(dc, center_x + 6, center_y - 6);
    } else if (tool == 7) {
        MoveToEx(dc, center_x - 7, center_y + 4, nullptr);
        LineTo(dc, center_x - 1, center_y - 6);
        LineTo(dc, center_x + 7, center_y + 2);
    } else if (tool == 8) {
        MoveToEx(dc, center_x - 7, center_y + 7, nullptr);
        LineTo(dc, center_x - 6, center_y + 1);
        LineTo(dc, center_x + 1, center_y - 6);
        LineTo(dc, center_x + 6, center_y - 1);
        LineTo(dc, center_x - 1, center_y + 6);
        LineTo(dc, center_x - 7, center_y + 7);
        MoveToEx(dc, center_x - 7, center_y + 7, nullptr);
        LineTo(dc, center_x - 1, center_y + 1);
    } else if (tool == 9) {
        POINT vertices[6]{};
        for (int i = 0; i < 6; ++i) {
            const double angle = -std::numbers::pi / 2.0 + 2.0 * std::numbers::pi * i / 6.0;
            vertices[i] = {center_x + static_cast<LONG>(std::lround(7.5 * std::cos(angle))),
                           center_y + static_cast<LONG>(std::lround(7.5 * std::sin(angle)))};
        }
        Polygon(dc, vertices, 6);
    } else if (tool == 10) {
        for (int r = -1; r <= 1; ++r) {
            const int y = center_y + r * 5;
            MoveToEx(dc, center_x - 7, y, nullptr);
            LineTo(dc, center_x + 7, y);
        }
    } else if (tool == 11) {
        Ellipse(dc, center_x - 2, center_y - 2, center_x + 2, center_y + 2);
        Ellipse(dc, center_x - 5, center_y - 5, center_x + 5, center_y + 5);
        Ellipse(dc, center_x - 8, center_y - 8, center_x + 8, center_y + 8);
    } else if (tool == 12) {
        Rectangle(dc, center_x - 6, center_y - 6, center_x + 2, center_y + 2);
        Rectangle(dc, center_x - 2, center_y - 2, center_x + 6, center_y + 6);
    }
    SelectObject(dc, old_brush);
    SelectObject(dc, old_pen);
    DeleteObject(pen);
}

void draw_tool_icon(HDC dc, int tool, int center_x, int center_y, COLORREF color) {
    auto* api = GdiplusApi::get();
    if (api) {
        GpGraphics* g = nullptr;
        if (api->CreateFromHDC(dc, &g) == 0 && g) {
            api->SetSmoothingMode(g, kSmoothingModeAntiAlias);
            api->SetPixelOffsetMode(g, kPixelOffsetModeHalf);
            draw_tool_icon_gdiplus(api, g, tool, static_cast<float>(center_x), static_cast<float>(center_y), color);
            api->DeleteGraphics(g);
            return;
        }
    }
    draw_tool_icon_gdi_fallback(dc, tool, center_x, center_y, color);
}

void draw_object_context_icon(HDC dc, ObjectContextAction action, int cx, int cy, COLORREF color) {
    HPEN pen = CreatePen(PS_SOLID, 1, color);
    const HGDIOBJ old_pen = SelectObject(dc, pen);
    const HGDIOBJ old_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    auto line = [&](int x1, int y1, int x2, int y2) {
        MoveToEx(dc, x1, y1, nullptr);
        LineTo(dc, x2, y2);
    };
    auto box = [&](int left, int top, int right, int bottom) {
        Rectangle(dc, left, top, right, bottom);
    };
    auto two_objects = [&] {
        box(cx - 7, cy - 5, cx + 1, cy + 2);
        box(cx - 1, cy - 2, cx + 7, cy + 5);
    };

    switch (action) {
    case ObjectContextAction::AlignLeft:
        line(cx - 7, cy - 7, cx - 7, cy + 7);
        box(cx - 7, cy - 5, cx + 4, cy - 1);
        box(cx - 7, cy + 1, cx + 1, cy + 5);
        break;
    case ObjectContextAction::AlignCenterHorizontal:
        line(cx, cy - 7, cx, cy + 7);
        box(cx - 5, cy - 5, cx + 5, cy - 1);
        box(cx - 3, cy + 1, cx + 3, cy + 5);
        break;
    case ObjectContextAction::AlignRight:
        line(cx + 7, cy - 7, cx + 7, cy + 7);
        box(cx - 4, cy - 5, cx + 7, cy - 1);
        box(cx - 1, cy + 1, cx + 7, cy + 5);
        break;
    case ObjectContextAction::AlignTop:
        line(cx - 7, cy - 7, cx + 7, cy - 7);
        box(cx - 5, cy - 7, cx - 1, cy + 5);
        box(cx + 1, cy - 7, cx + 5, cy + 1);
        break;
    case ObjectContextAction::AlignCenterVertical:
        line(cx - 7, cy, cx + 7, cy);
        box(cx - 5, cy - 5, cx - 1, cy + 5);
        box(cx + 1, cy - 3, cx + 5, cy + 3);
        break;
    case ObjectContextAction::AlignBottom:
        line(cx - 7, cy + 7, cx + 7, cy + 7);
        box(cx - 5, cy - 5, cx - 1, cy + 7);
        box(cx + 1, cy - 1, cx + 5, cy + 7);
        break;
    case ObjectContextAction::CenterPage:
        box(cx - 6, cy - 8, cx + 6, cy + 8);
        box(cx - 2, cy - 2, cx + 2, cy + 2);
        line(cx, cy - 5, cx, cy + 5);
        line(cx - 4, cy, cx + 4, cy);
        break;
    case ObjectContextAction::DistributeHorizontal:
        box(cx - 8, cy - 3, cx - 4, cy + 3);
        box(cx - 2, cy - 3, cx + 2, cy + 3);
        box(cx + 4, cy - 3, cx + 8, cy + 3);
        line(cx - 6, cy - 6, cx + 6, cy - 6);
        line(cx - 6, cy + 6, cx + 6, cy + 6);
        break;
    case ObjectContextAction::DistributeVertical:
        box(cx - 3, cy - 8, cx + 3, cy - 4);
        box(cx - 3, cy - 2, cx + 3, cy + 2);
        box(cx - 3, cy + 4, cx + 3, cy + 8);
        line(cx - 6, cy - 6, cx - 6, cy + 6);
        line(cx + 6, cy - 6, cx + 6, cy + 6);
        break;
    case ObjectContextAction::Group:
        two_objects();
        break;
    case ObjectContextAction::BooleanUnion:
        two_objects();
        line(cx - 2, cy, cx + 2, cy);
        line(cx, cy - 2, cx, cy + 2);
        break;
    case ObjectContextAction::BooleanDifference:
        two_objects();
        line(cx - 2, cy - 5, cx - 2, cy + 5);
        line(cx - 4, cy - 3, cx, cy + 3);
        break;
    case ObjectContextAction::BooleanIntersection: {
        two_objects();
        HBRUSH fill = CreateSolidBrush(color);
        const HGDIOBJ previous = SelectObject(dc, fill);
        Rectangle(dc, cx - 1, cy - 2, cx + 1, cy + 2);
        SelectObject(dc, previous);
        DeleteObject(fill);
        break;
    }
    case ObjectContextAction::BooleanExclude:
        two_objects();
        line(cx - 2, cy - 2, cx + 2, cy + 2);
        line(cx + 2, cy - 2, cx - 2, cy + 2);
        break;
    case ObjectContextAction::BooleanReverseDifference:
        two_objects();
        line(cx + 2, cy - 5, cx + 2, cy + 5);
        line(cx, cy - 3, cx + 4, cy + 3);
        break;
    case ObjectContextAction::FlipHorizontal:
        line(cx, cy - 6, cx, cy + 6);
        line(cx - 6, cy, cx - 1, cy - 4);
        line(cx - 1, cy - 4, cx - 1, cy + 4);
        line(cx - 1, cy + 4, cx - 6, cy);
        line(cx + 6, cy, cx + 1, cy - 4);
        line(cx + 1, cy - 4, cx + 1, cy + 4);
        line(cx + 1, cy + 4, cx + 6, cy);
        break;
    case ObjectContextAction::FlipVertical:
        line(cx - 6, cy, cx + 6, cy);
        line(cx, cy - 6, cx - 4, cy - 1);
        line(cx - 4, cy - 1, cx + 4, cy - 1);
        line(cx + 4, cy - 1, cx, cy - 6);
        line(cx, cy + 6, cx - 4, cy + 1);
        line(cx - 4, cy + 1, cx + 4, cy + 1);
        line(cx + 4, cy + 1, cx, cy + 6);
        break;
    case ObjectContextAction::Rotate90CW:
        Arc(dc, cx - 6, cy - 6, cx + 6, cy + 6, cx + 6, cy, cx - 6, cy);
        line(cx + 6, cy - 3, cx + 6, cy + 2);
        line(cx + 6, cy + 2, cx + 2, cy + 2);
        break;
    case ObjectContextAction::Rotate90CCW:
        Arc(dc, cx - 6, cy - 6, cx + 6, cy + 6, cx - 6, cy, cx + 6, cy);
        line(cx - 6, cy - 3, cx - 6, cy + 2);
        line(cx - 6, cy + 2, cx - 2, cy + 2);
        break;
    }
    SelectObject(dc, old_brush);
    SelectObject(dc, old_pen);
    DeleteObject(pen);
}

} // namespace

void NativeWindow::paint_document_tab_bar(HDC mem_dc, int w, int topbar_h, int tabbar_h) {
    bool is_dark = (theme_ == ThemeMode::CyberDark);
    COLORREF col_tab_bar_bg = is_dark ? RGB(10, 16, 25) : RGB(241, 245, 249);
    COLORREF col_tab_active = is_dark ? RGB(18, 28, 42) : RGB(255, 255, 255);
    COLORREF col_tab_inactive = is_dark ? RGB(12, 19, 29) : RGB(226, 232, 240);
    COLORREF col_border = is_dark ? RGB(29, 38, 54) : RGB(203, 213, 225);
    COLORREF col_teal = is_dark ? RGB(0, 242, 195) : RGB(0, 135, 106);
    COLORREF col_text_mut = is_dark ? RGB(148, 163, 184) : RGB(100, 116, 139);

    RECT rc_bar = { 0, topbar_h, w, topbar_h + tabbar_h };
    HBRUSH br_bar = CreateSolidBrush(col_tab_bar_bg);
    FillRect(mem_dc, &rc_bar, br_bar);
    DeleteObject(br_bar);

    HPEN pen_bdr = CreatePen(PS_SOLID, 1, col_border);
    HGDIOBJ old_pen = SelectObject(mem_dc, pen_bdr);
    MoveToEx(mem_dc, 0, topbar_h + tabbar_h, NULL);
    LineTo(mem_dc, w, topbar_h + tabbar_h);

    int cur_x = 10;
    int tab_w = 210;
    ensure_ui_fonts();
    HFONT font_tab = font_tab_;
    HFONT font_tab_bold = font_tab_bold_;
    HGDIOBJ old_font = SelectObject(mem_dc, font_tab);

    for (size_t i = 0; i < tabs_.size(); ++i) {
        const auto& tab = tabs_[i];
        bool is_act = (i == active_tab_index_);
        RECT rc_tab = { cur_x, topbar_h + 2, cur_x + tab_w, topbar_h + tabbar_h };

        HBRUSH br_t = CreateSolidBrush(is_act ? col_tab_active : col_tab_inactive);
        HPEN pen_t = CreatePen(PS_SOLID, 1, is_act ? col_teal : col_border);
        SelectObject(mem_dc, pen_t);
        SelectObject(mem_dc, br_t);
        RoundRect(mem_dc, rc_tab.left, rc_tab.top, rc_tab.right, rc_tab.bottom + 2, 4, 4);
        DeleteObject(pen_t);
        DeleteObject(br_t);

        if (is_act) {
            HPEN pen_top = CreatePen(PS_SOLID, 2, col_teal);
            SelectObject(mem_dc, pen_top);
            MoveToEx(mem_dc, rc_tab.left + 2, rc_tab.top, NULL);
            LineTo(mem_dc, rc_tab.right - 2, rc_tab.top);
            DeleteObject(pen_top);
        }

        SelectObject(mem_dc, is_act ? font_tab_bold : font_tab);
        SetTextColor(mem_dc, is_act ? col_teal : col_text_mut);
        std::wstring wtitle = L"📄 " + utf8_to_wide(tab.title);
        std::wstring wmode = utf8_to_wide(tab.color_mode);
        if (wtitle.find(wmode) == std::wstring::npos) {
            wtitle += L" (" + wmode + L")";
        }
        RECT rc_txt = { rc_tab.left + 8, rc_tab.top, rc_tab.right - 24, rc_tab.bottom };
        DrawTextW(mem_dc, wtitle.c_str(), -1, &rc_txt, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

        RECT rc_cls = { rc_tab.right - 22, rc_tab.top + 2, rc_tab.right - 4, rc_tab.bottom - 2 };
        SetTextColor(mem_dc, col_text_mut);
        DrawTextW(mem_dc, L"✕", -1, &rc_cls, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        cur_x += tab_w + 4;
    }

    RECT rc_new_tab = { cur_x, topbar_h + 3, cur_x + 26, topbar_h + tabbar_h - 2 };
    HBRUSH br_nt = CreateSolidBrush(col_tab_inactive);
    SelectObject(mem_dc, br_nt);
    RoundRect(mem_dc, rc_new_tab.left, rc_new_tab.top, rc_new_tab.right, rc_new_tab.bottom, 4, 4);
    DeleteObject(br_nt);
    SelectObject(mem_dc, font_tab_bold);
    SetTextColor(mem_dc, col_teal);
    DrawTextW(mem_dc, L"+", -1, &rc_new_tab, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    SelectObject(mem_dc, old_font);
    SelectObject(mem_dc, old_pen);
    DeleteObject(pen_bdr);
}

void NativeWindow::paint(HDC hdc, int w, int h) {
    ensure_ui_fonts();

    if (!backbuffer_dc_) {
        backbuffer_dc_ = CreateCompatibleDC(hdc);
    }
    if (backbuffer_bmp_ && (w != backbuffer_w_ || h != backbuffer_h_)) {
        SelectObject(backbuffer_dc_, backbuffer_old_bmp_);
        DeleteObject(backbuffer_bmp_);
        backbuffer_bmp_ = nullptr;
        backbuffer_old_bmp_ = nullptr;
    }
    if (!backbuffer_bmp_) {
        backbuffer_bmp_ = CreateCompatibleBitmap(hdc, w, h);
        backbuffer_old_bmp_ = SelectObject(backbuffer_dc_, backbuffer_bmp_);
        backbuffer_w_ = w;
        backbuffer_h_ = h;
    }
    HDC mem_dc = backbuffer_dc_;

    HFONT font_be_vietnam = font_be_vietnam_;
    HFONT font_be_vietnam_bold = font_be_vietnam_bold_;
    HFONT font_secondary = font_secondary_;
    HFONT font_mono = font_mono_;

    HGDIOBJ old_font = SelectObject(mem_dc, font_be_vietnam);

    bool is_dark = (theme_ == ThemeMode::CyberDark);
    bool is_vi = (language_ == UILanguage::Vietnamese);

    // 2026 Dual Theme Palette: Dark (Obsidian Teal) / Light (Silver Tech)
    COLORREF col_canvas = canvas_background_colorref();
    COLORREF col_header = is_dark ? RGB(12, 19, 29) : RGB(255, 255, 255);
    COLORREF col_ribbon = is_dark ? RGB(14, 22, 34) : RGB(248, 250, 252);
    COLORREF col_statusbar = is_dark ? RGB(7, 11, 16) : RGB(226, 232, 240);
    COLORREF col_border = is_dark ? RGB(29, 38, 54) : RGB(203, 213, 225);
    COLORREF col_teal = is_dark ? RGB(0, 242, 195) : RGB(0, 135, 106);
    COLORREF col_text_white = is_dark ? RGB(241, 245, 249) : RGB(15, 23, 42);
    COLORREF col_text_muted = is_dark ? RGB(148, 163, 184) : RGB(100, 116, 139);
    COLORREF col_active_tab = is_dark ? RGB(22, 37, 54) : RGB(236, 242, 248);
    COLORREF col_btn_bg = is_dark ? RGB(18, 28, 42) : RGB(241, 245, 249);
    COLORREF col_btn_bdr = is_dark ? RGB(29, 38, 54) : RGB(203, 213, 225);

    int topbar_h = 38, tabbar_h = 28, ribbon_h = 36, status_h = 26, tool_w = 52, inspect_w = 280;
    int content_top = topbar_h + tabbar_h + ribbon_h;

    SetBkMode(mem_dc, TRANSPARENT);

    if (show_home_screen_) {
        // Render Home Screen (Trang Home)
        paint_home_screen(mem_dc, w, h);
    } else {
        // Canvas is rasterized from the current immutable document snapshot.
        HBRUSH br_canvas = CreateSolidBrush(col_canvas);
        RECT rc_full = { 0, 0, w, h };
        FillRect(mem_dc, &rc_full, br_canvas);
        DeleteObject(br_canvas);
        paint_document_canvas(mem_dc, w, h);

        // A focused Drop Shadow exposes a direct-manipulation handle. Its draft preview
        // is drawn at the candidate offset and commits only when the drag is released.
        const auto& effect_selection = root().vector_controller().selection().selected_ids();
        const auto effect_snapshot = root().document().current_snapshot();
        if (active_dock_tab_ == 0 && show_style_inspector_ && effect_selection.size() == 1 &&
            effect_snapshot && workflow().can_edit_selected_effect(property_effect_index_)) {
            const auto effect_appearance = selection_appearance(*effect_snapshot, effect_selection);
            if (effect_appearance.can_edit_effect(property_effect_index_)) {
                const auto& selected_effect = effect_appearance.effects[property_effect_index_];
                const auto* shadow = std::get_if<document::DropShadowEffect>(
                    &selected_effect.parameters);
                const auto draft = workflow().effect_draft();
                if (draft && draft->editing_existing &&
                    property_effect_index_ < effect_appearance.effects.size()) {
                    if (const auto* draft_shadow = std::get_if<document::DropShadowEffect>(
                            &draft->parameters))
                        shadow = draft_shadow;
                }
                const auto bounds = root().vector_controller().selection().bounds(*effect_snapshot);
                if (shadow && bounds) {
                    const auto center = bounds->center();
                    const geometry::Point2D handle_world{
                        center.x + shadow->dx_mm, center.y + shadow->dy_mm};
                    const auto area = canvas_rect(w, h);
                    const auto center_screen = root().renderer().camera().world_to_screen(center);
                    const auto handle_screen = root().renderer().camera().world_to_screen(handle_world);
                    const int cx = area.left + static_cast<int>(std::lround(center_screen.x));
                    const int cy = area.top + static_cast<int>(std::lround(center_screen.y));
                    const int hx = area.left + static_cast<int>(std::lround(handle_screen.x));
                    const int hy = area.top + static_cast<int>(std::lround(handle_screen.y));
                    const auto handle_pen = CreatePen(PS_SOLID, 2, col_teal);
                    const auto old_handle_pen = SelectObject(mem_dc, handle_pen);
                    MoveToEx(mem_dc, cx, cy, nullptr);
                    LineTo(mem_dc, hx, hy);
                    const auto handle_brush = CreateSolidBrush(col_text_white);
                    const auto old_handle_brush = SelectObject(mem_dc, handle_brush);
                    Ellipse(mem_dc, hx - 7, hy - 7, hx + 8, hy + 8);
                    SelectObject(mem_dc, old_handle_brush);
                    SelectObject(mem_dc, old_handle_pen);
                    DeleteObject(handle_brush);
                    DeleteObject(handle_pen);
                }
            }
        }

        // Document Tab Bar
        paint_document_tab_bar(mem_dc, w, topbar_h, tabbar_h);

        // Context Tool Ribbon
        HBRUSH br_ribbon = CreateSolidBrush(col_ribbon);
        RECT rc_ribbon = { 0, topbar_h + tabbar_h, w, content_top };
        FillRect(mem_dc, &rc_ribbon, br_ribbon);
        DeleteObject(br_ribbon);

        const auto snapshot = root().document().current_snapshot();
        const auto& selection = root().vector_controller().selection().selected_ids();

        auto draw_rib_btn = [&](int left, int right, const wchar_t* label, bool active, COLORREF text_col) {
            RECT rect{left, topbar_h + tabbar_h + 4, right, content_top - 4};
            HBRUSH brush = CreateSolidBrush(active ? col_active_tab : col_btn_bg);
            HPEN pen = CreatePen(PS_SOLID, 1, active ? col_teal : col_btn_bdr);
            HGDIOBJ old_p = SelectObject(mem_dc, pen);
            HGDIOBJ old_b = SelectObject(mem_dc, brush);
            RoundRect(mem_dc, rect.left, rect.top, rect.right, rect.bottom, 4, 4);
            SelectObject(mem_dc, old_p);
            SelectObject(mem_dc, old_b);
            DeleteObject(pen);
            DeleteObject(brush);
            SetTextColor(mem_dc, text_col);
            SelectObject(mem_dc, active ? font_be_vietnam_bold : font_be_vietnam);
            DrawTextW(mem_dc, label, -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        };

        auto draw_num_input = [&](int left, int right, const wchar_t* prefix,
                                  const std::string& current_val, NumericFieldId field_id,
                                  bool append_mm = true) {
            bool is_focused = (active_num_field_ == field_id);
            RECT rect{left, topbar_h + tabbar_h + 4, right, content_top - 4};
            HBRUSH brush = CreateSolidBrush(is_focused ? col_active_tab : col_btn_bg);
            HPEN pen = CreatePen(PS_SOLID, is_focused ? 2 : 1, is_focused ? col_teal : col_btn_bdr);
            HGDIOBJ old_p = SelectObject(mem_dc, pen);
            HGDIOBJ old_b = SelectObject(mem_dc, brush);
            RoundRect(mem_dc, rect.left, rect.top, rect.right, rect.bottom, 4, 4);
            SelectObject(mem_dc, old_p);
            SelectObject(mem_dc, old_b);
            DeleteObject(pen);
            DeleteObject(brush);

            std::wstring display_text;
            if (prefix && *prefix) {
                display_text += prefix;
                display_text += L" ";
            }
            if (is_focused) {
                display_text += utf8_to_wide(num_input_buffer_);
                display_text += L"|";
            } else {
                display_text += utf8_to_wide(current_val);
                if (append_mm)
                    display_text += L" mm";
            }

            SetTextColor(mem_dc, is_focused ? col_teal : col_text_white);
            SelectObject(mem_dc, font_mono);
            RECT rc_txt = { rect.left + 4, rect.top, rect.right - 4, rect.bottom };
            DrawTextW(mem_dc, display_text.c_str(), -1, &rc_txt, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        };

        const auto opacity_percent_text = [&](double opacity) {
            std::ostringstream value;
            value << std::fixed << std::setprecision(1) << opacity * 100.0;
            std::string text = value.str();
            if (text.size() > 2 && text.substr(text.size() - 2) == ".0")
                text.erase(text.size() - 2);
            return text + "%";
        };

        bool is_text_mode = (active_tool_ == 4 || is_inline_text_editing_ || is_text_object_selected());

        if ((active_tool_ == 8 && pen_tool_ && pen_tool_->active()) ||
            ((active_tool_ == 6 || active_tool_ == 7) && drawing_tool_ && drawing_tool_->active())) {
            SelectObject(mem_dc, font_be_vietnam_bold);
            SetTextColor(mem_dc, col_teal);
            RECT label{10, topbar_h + tabbar_h, 48, content_top};
            const wchar_t* tool_name = (active_tool_ == 8) ? L"Pen:" : (active_tool_ == 6 ? L"Line:" : L"Poly:");
            DrawTextW(mem_dc, tool_name, -1, &label,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            draw_num_input(52, 185, L"L:", pen_numeric_length_, NumericFieldId::PenLength, true);
            draw_num_input(192, 315, is_vi ? L"Góc:" : L"Angle:", pen_numeric_angle_, NumericFieldId::PenAngle, false);

            draw_rib_btn(325, 415, is_vi ? L"↵ Kết thúc" : L"↵ Finish",
                         false, col_teal);
            draw_rib_btn(422, 492, is_vi ? L"Esc Hủy" : L"Esc Cancel",
                         false, RGB(239, 68, 68));
            RECT hint{504, topbar_h + tabbar_h, 990, content_top};
            SetTextColor(mem_dc, col_text_muted);
            DrawTextW(mem_dc,
                is_vi ? L"Phím L: nhập dài · Tab: góc (° theo trục X) · Enter: tạo đoạn / kết thúc"
                      : L"Key L: length · Tab: angle (° from X axis) · Enter: create / finish",
                -1, &hint, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        } else if (active_tool_ == 11 && node_tool_ && node_tool_->active()) {
            SelectObject(mem_dc, font_be_vietnam_bold);
            SetTextColor(mem_dc, col_teal);
            RECT label{10, topbar_h + tabbar_h, 85, content_top};
            DrawTextW(mem_dc, L"Node (F10):", -1, &label,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            const auto mode_opt = node_tool_->selected_nodes_mode();
            const bool is_cusp = mode_opt && (*mode_opt == document::PathNodeMode::Cusp);
            const bool is_smooth = mode_opt && (*mode_opt == document::PathNodeMode::Smooth);
            const bool is_symm = mode_opt && (*mode_opt == document::PathNodeMode::Symmetric);

            draw_rib_btn(90, 165, is_vi ? L"+ Node" : L"+ Node", false, col_teal);
            draw_rib_btn(170, 245, is_vi ? L"- Node" : L"- Node", false, RGB(239, 68, 68));
            draw_rib_btn(250, 325, is_vi ? L"Tách" : L"Break", false, col_text_white);
            draw_rib_btn(330, 405, is_vi ? L"Nối" : L"Join", false, col_text_white);
            draw_rib_btn(410, 485, is_vi ? L"Đóng" : L"Close", false, col_text_white);
            draw_rib_btn(490, 570, is_vi ? L"Cusp" : L"Cusp", is_cusp, is_cusp ? col_teal : col_text_white);
            draw_rib_btn(575, 655, is_vi ? L"Smooth" : L"Smooth", is_smooth, is_smooth ? col_teal : col_text_white);
            draw_rib_btn(660, 740, is_vi ? L"Symm" : L"Symm", is_symm, is_symm ? col_teal : col_text_white);
            draw_rib_btn(745, 830, is_vi ? L"→ Thẳng" : L"→ Line", false, col_text_white);
            draw_rib_btn(835, 920, is_vi ? L"→ Cong" : L"→ Curve", false, col_text_white);
            draw_rib_btn(925, 1010, is_vi ? L"Đảo chiều" : L"Reverse", false, col_text_white);
        } else if (active_tool_ == 12 && contour_tool_ && contour_tool_->active()) {
            SelectObject(mem_dc, font_be_vietnam_bold);
            SetTextColor(mem_dc, col_teal);
            RECT label{10, topbar_h + tabbar_h, 75, content_top};
            DrawTextW(mem_dc, L"Contour:", -1, &label, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            const auto dir = contour_tool_->direction();
            draw_rib_btn(80, 145, is_vi ? L"Ngoài" : L"Outside",
                         dir == geometry::ContourDirection::Outside,
                         dir == geometry::ContourDirection::Outside ? col_teal : col_text_white);
            draw_rib_btn(150, 215, is_vi ? L"Trong" : L"Inside",
                         dir == geometry::ContourDirection::Inside,
                         dir == geometry::ContourDirection::Inside ? col_teal : col_text_white);
            draw_rib_btn(220, 295, is_vi ? L"Vào tâm" : L"To Center",
                         dir == geometry::ContourDirection::ToCenter,
                         dir == geometry::ContourDirection::ToCenter ? col_teal : col_text_white);

            draw_rib_btn(305, 330, L"-", false, col_text_white);
            std::wstringstream ss_sp;
            ss_sp << std::fixed << std::setprecision(1) << contour_tool_->spacing_mm() << L" mm";
            draw_rib_btn(335, 405, ss_sp.str().c_str(), false, col_teal);
            draw_rib_btn(410, 435, L"+", false, col_text_white);

            draw_rib_btn(445, 470, L"-", false, col_text_white);
            std::wstring st_label = std::to_wstring(contour_tool_->steps()) + (is_vi ? L" bước" : L" steps");
            draw_rib_btn(475, 535, st_label.c_str(), false, col_teal);
            draw_rib_btn(540, 565, L"+", false, col_text_white);

            const auto join = contour_tool_->join_type();
            draw_rib_btn(575, 630, is_vi ? L"Nhọn" : L"Miter",
                         join == geometry::JoinType::Miter,
                         join == geometry::JoinType::Miter ? col_teal : col_text_white);
            draw_rib_btn(635, 690, is_vi ? L"Tròn" : L"Round",
                         join == geometry::JoinType::Round,
                         join == geometry::JoinType::Round ? col_teal : col_text_white);
            draw_rib_btn(695, 750, is_vi ? L"Vát" : L"Bevel",
                         join == geometry::JoinType::Bevel,
                         join == geometry::JoinType::Bevel ? col_teal : col_text_white);

            draw_rib_btn(760, 825, is_vi ? L"Áp dụng" : L"Apply", false, col_teal);
            draw_rib_btn(830, 940, is_vi ? L"Rã viền (Ctrl+K)" : L"Break (Ctrl+K)", false, col_text_white);
        } else if (active_tool_ == 9) {
            SelectObject(mem_dc, font_be_vietnam_bold);
            SetTextColor(mem_dc, col_teal);
            RECT label{10, topbar_h + tabbar_h, 52, content_top};
            DrawTextW(mem_dc, is_vi ? L"Đa giác:" : L"Poly:", -1, &label,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            draw_num_input(58, 164, is_vi ? L"Cạnh:" : L"Sides:",
                           std::to_string(polygon_sides_), NumericFieldId::PolygonSides, false);
            SetTextColor(mem_dc, col_text_muted);
            SelectObject(mem_dc, font_secondary);
            RECT hint{174, topbar_h + tabbar_h, 780, content_top};
            DrawTextW(mem_dc,
                is_vi ? L"3–256 cạnh · Shift khóa góc 15° · Ctrl tạo từ tâm · Properties có thiết lập nâng cao"
                      : L"3–256 sides · Shift snaps angle to 15° · Ctrl draws from center · see Properties for advanced settings",
                -1, &hint, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        } else if (is_text_mode) {
            // TYPOGRAPHY & TEXT PROPERTY BAR (CorelDRAW v25 / MS Word)
            SelectObject(mem_dc, font_be_vietnam_bold);
            SetTextColor(mem_dc, col_teal);
            RECT rc_t_lbl = { 10, topbar_h + tabbar_h, 48, content_top };
            DrawTextW(mem_dc, is_vi ? L"Chữ:" : L"Text:", -1, &rc_t_lbl, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            // 1. Font Family Selector Button
            const wchar_t* font_names_w[] = {
                L"Be Vietnam Pro ▾",
                L"Segoe UI ▾",
                L"Arial ▾",
                L"Times New Roman ▾",
                L"JetBrains Mono ▾"
            };
            int fam_idx = font_family_index_ % 5;
            draw_rib_btn(52, 192, font_names_w[fam_idx], false, col_text_white);

            // 2. Font Size pt Controls
            RECT rc_sz_lbl = { 198, topbar_h + tabbar_h, 218, content_top };
            SetTextColor(mem_dc, col_text_muted);
            DrawTextW(mem_dc, L"Pt:", -1, &rc_sz_lbl, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            draw_rib_btn(220, 240, L"-", false, col_teal);
            std::wstring sz_str = std::to_wstring(static_cast<int>(font_size_pt_)) + L" pt";
            RECT rc_sz_val = { 242, topbar_h + tabbar_h, 292, content_top };
            SetTextColor(mem_dc, col_text_white);
            DrawTextW(mem_dc, sz_str.c_str(), -1, &rc_sz_val, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            draw_rib_btn(294, 314, L"+", false, col_teal);

            // 3. Styles: B, I, U
            draw_rib_btn(322, 350, L"B", font_bold_, font_bold_ ? col_teal : col_text_white);
            draw_rib_btn(354, 382, L"I", font_italic_, font_italic_ ? col_teal : col_text_white);
            draw_rib_btn(386, 414, L"U", font_underline_, font_underline_ ? col_teal : col_text_white);

            // 4. Alignment: Left, Center, Right (Compact icon buttons)
            bool is_left = (font_alignment_ == typography::TextAlignment::Left);
            bool is_center = (font_alignment_ == typography::TextAlignment::Center);
            bool is_right = (font_alignment_ == typography::TextAlignment::Right);
            draw_rib_btn(422, 462, L"|◀ L", is_left, is_left ? col_teal : col_text_white);
            draw_rib_btn(466, 506, L"▶|◀ C", is_center, is_center ? col_teal : col_text_white);
            draw_rib_btn(510, 550, L"▶| R", is_right, is_right ? col_teal : col_text_white);

            // 5. Convert to Curves (CorelDRAW Ctrl+Q) - Compact icon
            draw_rib_btn(556, 616, L"⤹ Q", false, col_teal);

            // 6. Table Tool Button (CorelDRAW v25 Table) - Compact icon
            draw_rib_btn(622, 692, L"▦ Table", false, col_teal);

            // 7. Delete button if text is selected
            if (!selection.empty()) {
                draw_rib_btn(698, 748, is_vi ? L"✕ Xóa" : L"✕ Del", false, RGB(239, 68, 68));
            }

            // 8. Color Palette Swatches
            int color_start_x = (!selection.empty()) ? 754 : 698;
            draw_rib_btn(color_start_x, color_start_x + 36, L"■", false, RGB(0, 242, 195));
            draw_rib_btn(color_start_x + 40, color_start_x + 76, L"■", false, RGB(14, 165, 233));
            draw_rib_btn(color_start_x + 80, color_start_x + 116, L"■", false, RGB(234, 179, 8));
            draw_rib_btn(color_start_x + 120, color_start_x + 156, L"■", false, RGB(239, 68, 68));
            if (selection.size() == 1 && workflow().can_set_selected_object_opacity() &&
                1044 <= w - inspect_w) {
                const auto appearance = selection_appearance(*snapshot, selection);
                draw_num_input(920, 1044, is_vi ? L"Độ mờ:" : L"Opacity:",
                               opacity_percent_text(appearance.object_opacity),
                               NumericFieldId::ObjectOpacity, false);
            }
        } else if (selection.empty()) {
            // PAGE SIZING MODE (CorelDRAW Page Property Bar - Direct Numeric Input + Compact Preset Combobox)
            SelectObject(mem_dc, font_be_vietnam_bold);
            SetTextColor(mem_dc, col_teal);
            RECT rc_p_lbl = { 10, topbar_h + tabbar_h, 50, content_top };
            DrawTextW(mem_dc, is_vi ? L"Khổ:" : L"Page:", -1, &rc_p_lbl, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            const double pw = snapshot->page().width_mm;
            const double ph = snapshot->page().height_mm;

            // Detect current preset
            int active_preset_idx = -1;
            for (size_t i = 0; i < kPagePresetEntries.size(); ++i) {
                const auto& p = kPagePresetEntries[i];
                if ((std::abs(pw - p.width_mm) < 1.0 && std::abs(ph - p.height_mm) < 1.0) ||
                    (std::abs(pw - p.height_mm) < 1.0 && std::abs(ph - p.width_mm) < 1.0)) {
                    active_preset_idx = static_cast<int>(i);
                    break;
                }
            }
            std::wstring preset_lbl = (active_preset_idx >= 0)
                ? kPagePresetEntries[active_preset_idx].name
                : (is_vi ? L"Tùy chỉnh" : L"Custom");

            // Combobox Button: [ A4              ▾ ]
            RECT rc_cbo = { 52, topbar_h + tabbar_h + 4, 165, content_top - 4 };
            HBRUSH br_cbo = CreateSolidBrush(is_dark ? RGB(20, 32, 48) : RGB(241, 245, 249));
            HPEN pen_cbo = CreatePen(PS_SOLID, 1, show_page_preset_dropdown_ ? col_teal : col_border);
            HGDIOBJ old_pcbo = SelectObject(mem_dc, pen_cbo);
            HGDIOBJ old_bcbo = SelectObject(mem_dc, br_cbo);
            RoundRect(mem_dc, rc_cbo.left, rc_cbo.top, rc_cbo.right, rc_cbo.bottom, 4, 4);
            SelectObject(mem_dc, old_pcbo);
            SelectObject(mem_dc, old_bcbo);
            DeleteObject(pen_cbo);
            DeleteObject(br_cbo);

            SelectObject(mem_dc, font_be_vietnam_bold);
            SetTextColor(mem_dc, show_page_preset_dropdown_ ? col_teal : col_text_white);
            RECT rc_cbo_txt = { rc_cbo.left + 8, rc_cbo.top, rc_cbo.right - 22, rc_cbo.bottom };
            DrawTextW(mem_dc, preset_lbl.c_str(), -1, &rc_cbo_txt, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            SetTextColor(mem_dc, show_page_preset_dropdown_ ? col_teal : col_text_muted);
            RECT rc_cbo_arr = { rc_cbo.right - 20, rc_cbo.top, rc_cbo.right - 6, rc_cbo.bottom };
            DrawTextW(mem_dc, show_page_preset_dropdown_ ? L"▲" : L"▾", -1, &rc_cbo_arr, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            std::ostringstream spw, sph;
            spw << std::fixed << std::setprecision(1) << pw;
            sph << std::fixed << std::setprecision(1) << ph;
            std::string pw_str = spw.str();
            std::string ph_str = sph.str();
            if (pw_str.size() > 2 && pw_str.substr(pw_str.size() - 2) == ".0") pw_str.erase(pw_str.size() - 2);
            if (ph_str.size() > 2 && ph_str.substr(ph_str.size() - 2) == ".0") ph_str.erase(ph_str.size() - 2);

            draw_num_input(175, 275, L"W:", pw_str, NumericFieldId::PageWidth);
            draw_num_input(283, 383, L"H:", ph_str, NumericFieldId::PageHeight);

            bool is_land = (pw > ph);
            draw_rib_btn(391, 455, is_vi ? L"▯ Dọc" : L"▯ Port", !is_land, !is_land ? col_teal : col_text_white);
            draw_rib_btn(461, 525, is_vi ? L"▭ Ngang" : L"▭ Land", is_land, is_land ? col_teal : col_text_white);
            draw_rib_btn(533, 665, L"CMYK · 300 DPI", false, col_teal);
            if (workflow().is_array_session_active()) {
                std::wstring array_text = is_vi ? L"Mảng sao chép: " : L"Array: ";
                if (!array_input_buffer_.empty()) {
                    for (char c : array_input_buffer_) array_text += static_cast<wchar_t>(c);
                } else {
                    array_text += (workflow().array_session().is_multiplier ? L"*" : L"/") +
                                  std::to_wstring(workflow().array_session().current_count);
                }
                array_text += is_vi ? L" (Gõ *N hoặc /N · Esc kết thúc)" : L" (Type *N or /N · Esc to end)";
                draw_rib_btn(675, (std::min)(960, w - inspect_w - 10), array_text.c_str(), true, col_teal);
            }
        } else {
            // OBJECT TRANSFORM & EDIT MODE (Selection Ribbon - Direct Numeric Input)
            const auto obj_id = selection.front();
            auto bounds_opt = workflow().get_object_bounds(obj_id);
            double obj_x = bounds_opt ? bounds_opt->x : 0.0;
            double obj_y = bounds_opt ? bounds_opt->y : 0.0;
            double obj_w = bounds_opt ? bounds_opt->width : 0.0;
            double obj_h = bounds_opt ? bounds_opt->height : 0.0;

            std::ostringstream sx, sy, sw, sh;
            sx << std::fixed << std::setprecision(1) << obj_x;
            sy << std::fixed << std::setprecision(1) << obj_y;
            sw << std::fixed << std::setprecision(1) << obj_w;
            sh << std::fixed << std::setprecision(1) << obj_h;

            draw_num_input(10, 95, L"X:", sx.str(), NumericFieldId::ObjectX);
            draw_num_input(100, 185, L"Y:", sy.str(), NumericFieldId::ObjectY);
            draw_num_input(190, 285, L"W:", sw.str(), NumericFieldId::ObjectWidth);

            const wchar_t* lock_lbl = aspect_ratio_locked_ ?
                (is_vi ? L"🔒 Khóa" : L"🔒 Lock") :
                (is_vi ? L"🔓 Mở" : L"🔓 Free");
            draw_rib_btn(290, 360, lock_lbl, aspect_ratio_locked_, aspect_ratio_locked_ ? col_teal : col_text_muted);

            draw_num_input(365, 460, L"H:", sh.str(), NumericFieldId::ObjectHeight);

            if (is_in_isolation_mode()) {
                draw_rib_btn(580, 670, is_vi ? L"⬅ Thoát (Esc)" : L"⬅ Exit (Esc)", true, col_teal);
            } else {
                if (selection.size() >= 2) {
                    const auto buttons = get_multi_object_context_buttons(workflow(), workflow().can_group_selected(), w - inspect_w);
                    const int button_y = (topbar_h + tabbar_h + 4 + content_top - 4) / 2;
                    for (const auto& button : buttons) {
                        if (button.starts_group) {
                            HPEN separator = CreatePen(PS_SOLID, 1, col_border);
                            const HGDIOBJ old_pen = SelectObject(mem_dc, separator);
                            MoveToEx(mem_dc, button.left - 4, topbar_h + tabbar_h + 8, nullptr);
                            LineTo(mem_dc, button.left - 4, content_top - 8);
                            SelectObject(mem_dc, old_pen);
                            DeleteObject(separator);
                        }
                        draw_rib_btn(button.left, button.left + button.width, L"", false, col_text_white);
                        draw_object_context_icon(mem_dc, button.action,
                            button.left + button.width / 2, button_y, col_text_white);
                    }
                } else if (selection.size() == 1) {
                    const int button_y = (topbar_h + tabbar_h + 4 + content_top - 4) / 2;
                    int rx = 580;
                    draw_rib_btn(rx, rx + 28, L"", false, col_text_white);
                    draw_object_context_icon(mem_dc, ObjectContextAction::FlipHorizontal, rx + 14, button_y, col_text_white);
                    rx += 32;

                    draw_rib_btn(rx, rx + 28, L"", false, col_text_white);
                    draw_object_context_icon(mem_dc, ObjectContextAction::FlipVertical, rx + 14, button_y, col_text_white);
                    rx += 32;

                    draw_rib_btn(rx, rx + 28, L"", false, col_text_white);
                    draw_object_context_icon(mem_dc, ObjectContextAction::Rotate90CW, rx + 14, button_y, col_text_white);
                    rx += 32;

                    draw_rib_btn(rx, rx + 28, L"", false, col_text_white);
                    draw_object_context_icon(mem_dc, ObjectContextAction::Rotate90CCW, rx + 14, button_y, col_text_white);
                    rx += 36;

                    if (workflow().is_group(selection.front())) {
                        draw_rib_btn(rx, rx + 66, is_vi ? L"⛶ Rã" : L"⛶ Ungroup", false, col_teal);
                        rx += 72;
                        draw_rib_btn(rx, rx + 66, is_vi ? L"🔍 Chui vào" : L"🔍 Edit", true, col_teal);
                        rx += 72;
                    }
                }
            }

            if (workflow().is_array_session_active()) {
                std::wstring array_text = is_vi ? L"Mảng sao chép: " : L"Array: ";
                if (!array_input_buffer_.empty()) {
                    for (char c : array_input_buffer_) array_text += static_cast<wchar_t>(c);
                } else {
                    array_text += (workflow().array_session().is_multiplier ? L"*" : L"/") +
                                  std::to_wstring(workflow().array_session().current_count);
                }
                array_text += is_vi ? L" (Gõ *N hoặc /N · Esc kết thúc)" : L" (Type *N or /N · Esc to end)";
                draw_rib_btn((std::max)(10, w - inspect_w - 280), w - inspect_w - 10, array_text.c_str(), true, col_teal);
            }

            int color_start_x = (selection.size() == 1 && workflow().is_group(selection.front())) ? 920
                              : (selection.size() == 1) ? 780
                              : (selection.size() >= 2) ? 1160
                              : is_in_isolation_mode() ? 676 : 580;
            draw_rib_btn(color_start_x, color_start_x + 36, L"■", false, RGB(0, 242, 195));
            draw_rib_btn(color_start_x + 40, color_start_x + 76, L"■", false, RGB(14, 165, 233));
            draw_rib_btn(color_start_x + 80, color_start_x + 116, L"■", false, RGB(234, 179, 8));
            draw_rib_btn(color_start_x + 120, color_start_x + 156, L"■", false, RGB(239, 68, 68));
            if (selection.size() == 1 && workflow().can_set_selected_object_opacity() &&
                944 <= w - inspect_w) {
                const auto appearance = selection_appearance(*snapshot, selection);
                draw_num_input(818, 944, is_vi ? L"Độ mờ:" : L"Opacity:",
                               opacity_percent_text(appearance.object_opacity),
                               NumericFieldId::ObjectOpacity, false);
            }
        }

        // 5. Left Tool Strip: direct tools plus Quick Shapes, Path Drawing and Appearance groups.
        HBRUSH br_tools = CreateSolidBrush(col_header);
        RECT rc_tools = { 0, content_top, tool_w, h - status_h };
        FillRect(mem_dc, &rc_tools, br_tools);
        DeleteObject(br_tools);

        const int toolbox_tools[] = {0, quick_shapes_last_tool_, path_drawing_last_tool_, 3, 4, 5, 10};
        const int group_for_row[] = {0, 1, 2, 0, 0, 0, 3};
        for (int row = 0; row < 7; ++row) {
            const int tool = toolbox_tools[row];
            const int group = group_for_row[row];
            const int ty = content_top + 10 + row * 42;
            RECT rc_tb = { 6, ty, tool_w - 6, ty + 36 };
            const bool active = group == 1 ? (active_tool_ == 1 || active_tool_ == 2 || active_tool_ == 9)
                              : group == 2 ? (active_tool_ >= 6 && active_tool_ <= 8)
                              : group == 3 ? (toolbox_open_group_ == 3)
                                           : active_tool_ == tool;
            const int btn_cx = (rc_tb.left + rc_tb.right) / 2;
            const int btn_cy = (rc_tb.top + rc_tb.bottom) / 2;
            if (active) {
                HBRUSH br_t_act = CreateSolidBrush(col_active_tab);
                HPEN pen_t_act = CreatePen(PS_SOLID, 1, col_teal);
                const HGDIOBJ old_pen = SelectObject(mem_dc, pen_t_act);
                const HGDIOBJ old_brush = SelectObject(mem_dc, br_t_act);
                RoundRect(mem_dc, rc_tb.left, rc_tb.top, rc_tb.right, rc_tb.bottom, 6, 6);
                SelectObject(mem_dc, old_brush);
                SelectObject(mem_dc, old_pen);
                DeleteObject(pen_t_act);
                DeleteObject(br_t_act);
                draw_tool_icon(mem_dc, tool, btn_cx, btn_cy, col_teal);
            } else {
                HBRUSH br_t_inact = CreateSolidBrush(col_header);
                HPEN pen_t_inact = CreatePen(PS_SOLID, 1, col_border);
                const HGDIOBJ old_pen = SelectObject(mem_dc, pen_t_inact);
                const HGDIOBJ old_brush = SelectObject(mem_dc, br_t_inact);
                RoundRect(mem_dc, rc_tb.left, rc_tb.top, rc_tb.right, rc_tb.bottom, 6, 6);
                SelectObject(mem_dc, old_brush);
                SelectObject(mem_dc, old_pen);
                DeleteObject(pen_t_inact);
                DeleteObject(br_t_inact);
                draw_tool_icon(mem_dc, tool, btn_cx, btn_cy, col_text_muted);
            }
            if (group != 0) {
                draw_toolbox_flyout_marker(mem_dc, rc_tb.right - 5, rc_tb.bottom - 5,
                                           active ? col_teal : RGB(100, 116, 139));
            }
            if (toolbox_keyboard_focus_ && group == toolbox_focus_group_) {
                RECT focus = rc_tb;
                InflateRect(&focus, -3, -3);
                DrawFocusRect(mem_dc, &focus);
            }
        }

        if (toolbox_open_group_ >= 1 && toolbox_open_group_ <= 3) {
            const int group_row = toolbox_open_group_ == 1 ? 1
                                : toolbox_open_group_ == 2 ? 2 : 6;
            const int flyout_top = content_top + 10 + group_row * 42;
            const int flyout_left = 54;
            const int flyout_width = toolbox_open_group_ == 3 ? 208 : 188;
            RECT flyout{flyout_left, flyout_top, flyout_left + flyout_width, flyout_top + 116};
            HBRUSH flyout_brush = CreateSolidBrush(is_dark ? RGB(20, 31, 46) : RGB(250, 252, 255));
            HPEN flyout_pen = CreatePen(PS_SOLID, 1, col_border);
            const HGDIOBJ old_flyout_pen = SelectObject(mem_dc, flyout_pen);
            const HGDIOBJ old_flyout_brush = SelectObject(mem_dc, flyout_brush);
            RoundRect(mem_dc, flyout.left, flyout.top, flyout.right, flyout.bottom, 7, 7);
            SelectObject(mem_dc, old_flyout_brush);
            SelectObject(mem_dc, old_flyout_pen);
            DeleteObject(flyout_brush);
            DeleteObject(flyout_pen);

            const bool quick_shapes = toolbox_open_group_ == 1;
            const bool path_drawing = toolbox_open_group_ == 2;
            const bool appearance = toolbox_open_group_ == 3;
            SelectObject(mem_dc, font_be_vietnam_bold);
            SetTextColor(mem_dc, col_teal);
            RECT flyout_title{flyout_left + 10, flyout_top + 3, flyout.right - 8, flyout_top + 25};
            const wchar_t *title = quick_shapes
                ? (is_vi ? L"Hình cơ bản · Quick Shapes" : L"Quick Shapes")
                : path_drawing
                    ? (is_vi ? L"Đường vẽ · Path Drawing" : L"Path Drawing")
                    : (is_vi ? L"Hiệu ứng · Appearance" : L"Effects · Appearance");
            DrawTextW(mem_dc, title,
                -1, &flyout_title, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            const int flyout_tools[] = {
                quick_shapes ? 1 : path_drawing ? 6 : 10,
                quick_shapes ? 2 : path_drawing ? 7 : 11,
                quick_shapes ? 9 : path_drawing ? 8 : 12};
            const wchar_t* labels_en[] = {L"Rectangle", L"Ellipse", L"Polygon"};
            const wchar_t* labels_vi[] = {L"Hình chữ nhật", L"Hình elip", L"Đa giác"};
            const wchar_t* path_en[] = {L"Line", L"Polyline", L"Pen"};
            const wchar_t* path_vi[] = {L"Đoạn thẳng", L"Đường gấp khúc", L"Pen"};
            const wchar_t* appearance_en[] = {L"Appearance Properties", L"Gaussian Blur", L"Drop Shadow"};
            const wchar_t* appearance_vi[] = {L"Thuộc tính diện mạo", L"Gaussian Blur", L"Drop Shadow"};
            const bool has_blur = appearance && workflow().selected_effect_index(
                document::GaussianBlurEffect{1.0, 1.0}).has_value();
            const bool has_shadow = appearance && workflow().selected_effect_index(
                document::DropShadowEffect{}).has_value();
            for (int item = 0; item < 3; ++item) {
                const int item_top = flyout_top + 26 + item * 29;
                const bool selected = appearance
                    ? (item == 1 && has_blur) || (item == 2 && has_shadow)
                    : active_tool_ == flyout_tools[item];
                const bool available = !appearance || item == 0 ||
                    (item == 1 && workflow().can_activate_selected_effect(
                        document::GaussianBlurEffect{1.0, 1.0})) ||
                    (item == 2 && workflow().can_activate_selected_effect(
                        document::DropShadowEffect{}));
                RECT item_rect{flyout_left + 5, item_top, flyout.right - 5, item_top + 28};
                if (selected || (toolbox_flyout_index_ == item && available)) {
                    HBRUSH item_brush = CreateSolidBrush(selected ? col_active_tab : col_header);
                    FillRect(mem_dc, &item_rect, item_brush);
                    DeleteObject(item_brush);
                }
                draw_tool_icon(mem_dc, flyout_tools[item], flyout_left + 20,
                               item_top + 14, !available ? col_text_muted : selected ? col_teal : col_text_white);
                SetTextColor(mem_dc, !available ? col_text_muted : selected ? col_teal : col_text_white);
                SelectObject(mem_dc, selected ? font_be_vietnam_bold : font_be_vietnam);
                RECT item_label{flyout_left + 35, item_top, flyout.right - 8, item_top + 28};
                const wchar_t* label = quick_shapes
                    ? (is_vi ? labels_vi[item] : labels_en[item])
                    : path_drawing
                        ? (is_vi ? path_vi[item] : path_en[item])
                        : (is_vi ? appearance_vi[item] : appearance_en[item]);
                DrawTextW(mem_dc, label, -1, &item_label,
                          DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            }
        }

        // 6. Right Inspector & Layers Dock
        int insp_x = w - inspect_w;
        HBRUSH br_dock = CreateSolidBrush(col_header);
        RECT rc_dock = { insp_x, content_top, w, h - status_h };
        FillRect(mem_dc, &rc_dock, br_dock);
        DeleteObject(br_dock);

        int tab_w = inspect_w / 3;
        const wchar_t* tab_names_vi[] = { L"Thuộc tính", L"Lớp (Layers)", L"Lịch sử" };
        const wchar_t* tab_names_en[] = { L"Inspector", L"Layers", L"History" };
        for (int i = 0; i < 3; ++i) {
            RECT rc_tab = { insp_x + i * tab_w, content_top, insp_x + (i + 1) * tab_w, content_top + 28 };
            if (active_dock_tab_ == i) {
                HBRUSH br_tab = CreateSolidBrush(col_active_tab);
                HPEN p_tab = CreatePen(PS_SOLID, 1, col_teal);
                SelectObject(mem_dc, p_tab);
                SelectObject(mem_dc, br_tab);
                RoundRect(mem_dc, rc_tab.left + 2, rc_tab.top + 2, rc_tab.right - 2, rc_tab.bottom, 6, 6);
                DeleteObject(p_tab);
                DeleteObject(br_tab);
                SetTextColor(mem_dc, col_teal);
                SelectObject(mem_dc, font_be_vietnam_bold);
            } else {
                SetTextColor(mem_dc, col_text_muted);
                SelectObject(mem_dc, font_secondary);
            }
            DrawTextW(mem_dc, is_vi ? tab_names_vi[i] : tab_names_en[i], -1, &rc_tab, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }

        const int content_y = content_top + 36;
        SelectObject(mem_dc, font_be_vietnam_bold);
        SetTextColor(mem_dc, col_teal);
        RECT rc_head = { insp_x + 14, content_y,
                         active_dock_tab_ == 0 ? w - 100 : w - 14, content_y + 22 };
        SelectObject(mem_dc, font_secondary);
        SetTextColor(mem_dc, col_text_white);

        if (active_dock_tab_ == 0 && active_tool_ == 9) {
            SelectObject(mem_dc, font_be_vietnam_bold);
            SetTextColor(mem_dc, col_teal);
            RECT properties_title{insp_x + 14, content_y + 2, w - 14, content_y + 25};
            DrawTextW(mem_dc, is_vi ? L"THUỘC TÍNH · ĐA GIÁC" : L"PROPERTIES · POLYGON", -1,
                      &properties_title, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            const auto draw_polygon_property = [&](int y, const wchar_t* name,
                                                   const std::wstring& value,
                                                   NumericFieldId field) {
                RECT name_rect{insp_x + 14, y, insp_x + 126, y + 32};
                SetTextColor(mem_dc, col_text_white);
                SelectObject(mem_dc, font_be_vietnam);
                DrawTextW(mem_dc, name, -1, &name_rect,
                          DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                RECT input{insp_x + 132, y + 2, w - 14, y + 30};
                const bool focused = active_num_field_ == field;
                HBRUSH input_brush = CreateSolidBrush(focused ? col_active_tab : col_btn_bg);
                HPEN input_pen = CreatePen(PS_SOLID, focused ? 2 : 1,
                                           focused ? col_teal : col_btn_bdr);
                const HGDIOBJ old_pen = SelectObject(mem_dc, input_pen);
                const HGDIOBJ old_brush = SelectObject(mem_dc, input_brush);
                RoundRect(mem_dc, input.left, input.top, input.right, input.bottom, 5, 5);
                SelectObject(mem_dc, old_brush);
                SelectObject(mem_dc, old_pen);
                DeleteObject(input_brush);
                DeleteObject(input_pen);
                RECT value_rect{input.left + 8, input.top, input.right - 8, input.bottom};
                SetTextColor(mem_dc, focused ? col_teal : col_text_white);
                SelectObject(mem_dc, font_mono);
                const std::wstring shown = focused ? utf8_to_wide(num_input_buffer_) + L"|" : value;
                DrawTextW(mem_dc, shown.c_str(), -1, &value_rect,
                          DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            };
            draw_polygon_property(content_y + 34, is_vi ? L"Số cạnh" : L"Sides",
                std::to_wstring(polygon_sides_), NumericFieldId::PolygonSides);
            std::wostringstream rotation_value;
            rotation_value << std::fixed << std::setprecision(1) << polygon_rotation_degrees_;
            std::wstring rotation_text = rotation_value.str();
            if (rotation_text.size() > 2 && rotation_text.substr(rotation_text.size() - 2) == L".0")
                rotation_text.erase(rotation_text.size() - 2);
            rotation_text += L"°";
            draw_polygon_property(content_y + 78, is_vi ? L"Góc xoay" : L"Rotation",
                rotation_text, NumericFieldId::PolygonRotation);

            SelectObject(mem_dc, font_secondary);
            SetTextColor(mem_dc, col_text_muted);
            RECT property_hint{insp_x + 14, content_y + 122, w - 14, content_y + 208};
            DrawTextW(mem_dc,
                is_vi ? L"Số cạnh: 3–256. Góc xoay là góc đỉnh đầu tiên. Shift khóa hướng kéo theo bước 15°; Ctrl lấy điểm đầu làm tâm. Các giá trị này áp dụng cho đa giác tạo tiếp theo và không sửa path đã có."
                      : L"Sides: 3–256. Rotation sets the first vertex angle. Shift snaps the drag direction to 15°; Ctrl uses the start point as center. Settings apply to the next polygon and do not alter existing paths.",
                -1, &property_hint, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
        } else if (active_dock_tab_ == 0) {
            const int panel_left = insp_x + AlignPanelMetrics::content_left;
            const int section_width = (AlignPanelMetrics::content_width - 12) / 3;
            const wchar_t* section_labels_vi[] = {L"Đối tượng", L"Kiểu", L"Căn chỉnh"};
            const wchar_t* section_labels_en[] = {L"Objects", L"Style", L"Align"};
            for (int i = 0; i < 3; ++i) {
                const bool active = i == 0 ? (!show_style_inspector_ && !show_align_panel_)
                    : i == 1 ? show_style_inspector_ : show_align_panel_;
                RECT section_button{
                    panel_left + i * (section_width + 6),
                    AlignPanelMetrics::section_tabs_top,
                    panel_left + i * (section_width + 6) + section_width,
                    AlignPanelMetrics::section_tabs_top + AlignPanelMetrics::section_tabs_height - 2};
                HBRUSH section_brush = CreateSolidBrush(active ? col_active_tab : col_btn_bg);
                HPEN section_pen = CreatePen(PS_SOLID, 1, active ? col_teal : col_btn_bdr);
                const auto old_pen = SelectObject(mem_dc, section_pen);
                const auto old_brush = SelectObject(mem_dc, section_brush);
                RoundRect(mem_dc, section_button.left, section_button.top, section_button.right,
                          section_button.bottom, 5, 5);
                SelectObject(mem_dc, old_pen);
                SelectObject(mem_dc, old_brush);
                DeleteObject(section_pen);
                DeleteObject(section_brush);
                SetTextColor(mem_dc, active ? col_teal : col_text_white);
                DrawTextW(mem_dc, is_vi ? section_labels_vi[i] : section_labels_en[i], -1,
                          &section_button, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            }
        }

        std::wostringstream content;
        if (active_dock_tab_ == 0) {
            if (show_align_panel_) {
                using Metrics = AlignPanelMetrics;
                const int panel_left = insp_x + Metrics::content_left;
                const int three_button_width = (Metrics::content_width - 12) / 3;
                const int two_button_width = (Metrics::content_width - 6) / 2;
                const auto draw_panel_button = [&](int x, int y, int width, int height,
                                                   const wchar_t* label, bool enabled, bool active = false) {
                    const COLORREF fill = !enabled ? col_header : active ? col_active_tab : col_btn_bg;
                    const COLORREF border = active ? col_teal : enabled ? col_btn_bdr : col_border;
                    const COLORREF text = !enabled ? col_text_muted : active ? col_teal : col_text_white;
                    HBRUSH brush = CreateSolidBrush(fill);
                    HPEN pen = CreatePen(PS_SOLID, 1, border);
                    const auto old_pen = SelectObject(mem_dc, pen);
                    const auto old_brush = SelectObject(mem_dc, brush);
                    RoundRect(mem_dc, x, y, x + width, y + height, 4, 4);
                    SelectObject(mem_dc, old_pen);
                    SelectObject(mem_dc, old_brush);
                    DeleteObject(pen);
                    DeleteObject(brush);
                    SetTextColor(mem_dc, text);
                    RECT rect{x + 2, y, x + width - 2, y + height};
                    DrawTextW(mem_dc, label, -1, &rect,
                              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
                };
                const auto draw_heading = [&](int y, const wchar_t* label) {
                    SetTextColor(mem_dc, col_teal);
                    RECT rect{panel_left, y, panel_left + Metrics::content_width, y + 18};
                    DrawTextW(mem_dc, label, -1, &rect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                };

                draw_heading(164, is_vi ? L"CĂN THEO" : L"ALIGN REFERENCE");
                draw_panel_button(panel_left + Metrics::content_width - 98, 164, 98, 20,
                    is_vi ? L"Giữa trang" : L"Center Page",
                    workflow().can_align_selected(geometry::AlignMode::CenterPage));

                constexpr geometry::AlignReference references[] = {
                    geometry::AlignReference::Selection,
                    geometry::AlignReference::KeyObject,
                    geometry::AlignReference::Page
                };
                const wchar_t* reference_labels_en[] = {L"Selection", L"Key Object", L"Page"};
                const wchar_t* reference_labels_vi[] = {L"Vùng chọn", L"Đối tượng", L"Trang"};
                for (int i = 0; i < 3; ++i) {
                    const bool enabled = i != 1 || selection.size() >= 2;
                    draw_panel_button(panel_left + i * (three_button_width + 6), Metrics::reference_top,
                        three_button_width, Metrics::reference_height,
                        is_vi ? reference_labels_vi[i] : reference_labels_en[i], enabled,
                        align_reference_ == references[i]);
                }

                draw_heading(216, is_vi ? L"CĂN CHỈNH ĐỐI TƯỢNG" : L"ALIGN OBJECTS");
                constexpr geometry::AlignMode align_modes[] = {
                    geometry::AlignMode::Left,
                    geometry::AlignMode::CenterHorizontal,
                    geometry::AlignMode::Right,
                    geometry::AlignMode::Top,
                    geometry::AlignMode::CenterVertical,
                    geometry::AlignMode::Bottom
                };
                const wchar_t* align_labels_en[] = {L"Left", L"Center X", L"Right", L"Top", L"Center Y", L"Bottom"};
                const wchar_t* align_labels_vi[] = {L"Trái", L"Giữa X", L"Phải", L"Trên", L"Giữa Y", L"Dưới"};
                const int align_row_gap = Metrics::align_button_height + Metrics::align_row_gap;
                for (int i = 0; i < 6; ++i) {
                    const int row = i / 3;
                    const int column = i % 3;
                    const int y = Metrics::align_top + row * align_row_gap;
                    draw_panel_button(panel_left + column * (three_button_width + 6), y,
                        three_button_width, Metrics::align_button_height,
                        is_vi ? align_labels_vi[i] : align_labels_en[i],
                        workflow().can_align_selected(align_modes[i], align_reference_, key_object_id_));
                }

                draw_heading(300, is_vi ? L"PHÂN BỔ ĐỀU" : L"DISTRIBUTE EVENLY");
                SetTextColor(mem_dc, col_text_muted);
                RECT horizontal_label{panel_left, 320, panel_left + Metrics::content_width, 338};
                DrawTextW(mem_dc, is_vi ? L"Ngang" : L"Horizontal", -1, &horizontal_label,
                          DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                constexpr geometry::DistributeMode horizontal_modes[] = {
                    geometry::DistributeMode::SpacingHorizontal,
                    geometry::DistributeMode::CentersHorizontal,
                    geometry::DistributeMode::LeftEdges,
                    geometry::DistributeMode::RightEdges
                };
                const wchar_t* horizontal_labels_en[] = {L"Equal gap", L"Centers", L"Left edges", L"Right edges"};
                const wchar_t* horizontal_labels_vi[] = {L"Khoảng hở", L"Tâm", L"Mép trái", L"Mép phải"};
                const int distribute_row_gap = Metrics::distribute_button_height + Metrics::distribute_row_gap;
                for (int i = 0; i < 4; ++i) {
                    const int row = i / 2;
                    const int column = i % 2;
                    draw_panel_button(panel_left + column * (two_button_width + 6),
                        Metrics::horizontal_top + row * distribute_row_gap,
                        two_button_width, Metrics::distribute_button_height,
                        is_vi ? horizontal_labels_vi[i] : horizontal_labels_en[i],
                        workflow().can_distribute_selected(horizontal_modes[i]));
                }

                RECT vertical_label{panel_left, 400, panel_left + Metrics::content_width, 418};
                SetTextColor(mem_dc, col_text_muted);
                DrawTextW(mem_dc, is_vi ? L"Dọc" : L"Vertical", -1, &vertical_label,
                          DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                constexpr geometry::DistributeMode vertical_modes[] = {
                    geometry::DistributeMode::SpacingVertical,
                    geometry::DistributeMode::CentersVertical,
                    geometry::DistributeMode::TopEdges,
                    geometry::DistributeMode::BottomEdges
                };
                const wchar_t* vertical_labels_en[] = {L"Equal gap", L"Centers", L"Top edges", L"Bottom edges"};
                const wchar_t* vertical_labels_vi[] = {L"Khoảng hở", L"Tâm", L"Mép trên", L"Mép dưới"};
                for (int i = 0; i < 4; ++i) {
                    const int row = i / 2;
                    const int column = i % 2;
                    draw_panel_button(panel_left + column * (two_button_width + 6),
                        Metrics::vertical_top + row * distribute_row_gap,
                        two_button_width, Metrics::distribute_button_height,
                        is_vi ? vertical_labels_vi[i] : vertical_labels_en[i],
                        workflow().can_distribute_selected(vertical_modes[i]));
                }

                draw_heading(481, is_vi ? L"KHOẢNG HỞ CỐ ĐỊNH (mm)" : L"FIXED GAP (mm)");
                const bool spacing_editing = active_num_field_ == NumericFieldId::DistributeSpacing;
                std::wostringstream spacing_text;
                if (spacing_editing) spacing_text << utf8_to_wide(num_input_buffer_);
                else spacing_text << std::fixed << std::setprecision(1) << distribute_spacing_mm_;
                RECT spacing_field{panel_left, Metrics::gap_field_top,
                                   panel_left + 166, Metrics::gap_field_top + Metrics::gap_field_height};
                HBRUSH spacing_brush = CreateSolidBrush(spacing_editing ? col_active_tab : col_btn_bg);
                HPEN spacing_pen = CreatePen(PS_SOLID, 1, spacing_editing ? col_teal : col_btn_bdr);
                const auto old_spacing_pen = SelectObject(mem_dc, spacing_pen);
                const auto old_spacing_brush = SelectObject(mem_dc, spacing_brush);
                RoundRect(mem_dc, spacing_field.left, spacing_field.top, spacing_field.right,
                          spacing_field.bottom, 4, 4);
                SelectObject(mem_dc, old_spacing_pen);
                SelectObject(mem_dc, old_spacing_brush);
                DeleteObject(spacing_pen);
                DeleteObject(spacing_brush);
                RECT spacing_value{spacing_field.left + 8, spacing_field.top,
                                   spacing_field.right - 8, spacing_field.bottom};
                SetTextColor(mem_dc, spacing_editing ? col_teal : col_text_white);
                const auto spacing_value_text = spacing_text.str();
                DrawTextW(mem_dc, spacing_value_text.c_str(), -1, &spacing_value,
                          DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                RECT spacing_unit{panel_left + 170, Metrics::gap_field_top,
                                  panel_left + Metrics::content_width, Metrics::gap_field_top + Metrics::gap_field_height};
                SetTextColor(mem_dc, col_text_muted);
                DrawTextW(mem_dc, L"mm", -1, &spacing_unit, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                draw_panel_button(panel_left, Metrics::apply_top, two_button_width, Metrics::apply_height,
                    is_vi ? L"Áp ngang" : L"Apply Horizontal",
                    workflow().can_distribute_selected(geometry::DistributeMode::SpacingHorizontal, distribute_spacing_mm_));
                draw_panel_button(panel_left + two_button_width + 6, Metrics::apply_top,
                    two_button_width, Metrics::apply_height,
                    is_vi ? L"Áp dọc" : L"Apply Vertical",
                    workflow().can_distribute_selected(geometry::DistributeMode::SpacingVertical, distribute_spacing_mm_));

                SetTextColor(mem_dc, col_text_muted);
                RECT panel_hint{panel_left, 570, panel_left + Metrics::content_width, 626};
                DrawTextW(mem_dc,
                    is_vi ? L"Chia khoảng hở tự động cần 3 đối tượng; khoảng hở cố định cần 2. Ctrl+Z để hoàn tác."
                          : L"Automatic gaps need 3 objects; fixed gaps need 2. Ctrl+Z to undo.",
                    -1, &panel_hint, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
            } else if (show_style_inspector_) {
                paint_style_inspector(mem_dc, insp_x, content_y + 24, h - status_h);
            } else {
            DrawTextW(mem_dc, is_vi ? L"THUỘC TÍNH ĐANG CHỌN" : L"SELECTION", -1,
                      &rc_head, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
            content << (is_vi ? L"Số đối tượng: " : L"Selected objects: ") << selection.size();
            if (selection.size() == 1) {
                const auto sel_id = selection.front();
                const auto object = snapshot->objects().get(sel_id);
                if (object) {
                    const auto name = std::visit([](const auto& value) { return value.name; }, *object);
                    content << L"\n" << utf8_to_wide(name);
                    if (workflow().is_group(sel_id)) {
                        const auto& grp = std::get<document::GroupObject>(*object);
                        content << (is_vi ? L"\n[Nhóm đối tượng - " : L"\n[Group - ")
                                << grp.children_ids.size() << (is_vi ? L" hình]" : L" items]");
                    }
                    if (const auto* im = std::get_if<document::ImageObject>(&*object)) {
                        content << (is_vi ? L"\n[Ảnh Bitmap - " : L"\n[Raster Image - ")
                                << im->width_px << L"×" << im->height_px << (is_vi ? L" px]" : L" px]");
                    }
                    if (const auto bounds = workflow().get_object_bounds(sel_id)) {
                        content << L"\nX: " << std::fixed << std::setprecision(1) << bounds->x
                                << L" mm\nY: " << bounds->y << L" mm\nW: " << bounds->width
                                << L" mm\nH: " << bounds->height << L" mm";
                    }
                    if (workflow().is_group(sel_id) && !is_in_isolation_mode()) {
                        content << (is_vi ? L"\nNhấp đúp/Enter để chui vào nhóm."
                                          : L"\nDouble-click/Enter to edit inside.");
                    }
                }
            } else {
                content << (is_vi ? L"\nNhấp một đối tượng để xem kích thước (mm)."
                                  : L"\nSelect an object to inspect its size in mm.");
            }
            if (is_in_isolation_mode()) {
                content << (is_vi ? L"\n\n⚡ ĐANG CÔ LẬP NHÓM\nNhấn Esc để thoát."
                                  : L"\n\n⚡ GROUP ISOLATION MODE\nPress Esc to exit.");
            }
            RECT body = { insp_x + 14, content_y + 28, w - 14, content_y + 190 };
            const auto text = content.str();
            DrawTextW(mem_dc, text.c_str(), -1, &body, DT_LEFT | DT_NOPREFIX);
            SelectObject(mem_dc, font_secondary);
            SetTextColor(mem_dc, col_text_muted);
            RECT action_hint = { insp_x + 14, content_y + 198, w - 14, content_y + 254 };
            const wchar_t* action_hint_text = selection.size() >= 2
                ? (is_vi ? L"Lệnh căn chỉnh và tạo hình đủ điều kiện xuất hiện trên thanh ngữ cảnh."
                         : L"Eligible Align and Shaping commands appear on the context bar.")
                : (is_vi ? L"Chọn đối tượng để hiện lệnh căn chỉnh và tạo hình phù hợp."
                         : L"Select objects to show eligible Align and Shaping commands.");
            DrawTextW(mem_dc, action_hint_text, -1, &action_hint,
                      DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
            }
        } else if (active_dock_tab_ == 1) {
            DrawTextW(mem_dc, is_vi ? L"ĐỐI TƯỢNG TRONG TÀI LIỆU" : L"DOCUMENT OBJECTS", -1,
                      &rc_head, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
            size_t shown = 0;
            for (const auto& [id, object] : snapshot->objects().all()) {
                const auto name = std::visit([](const auto& value) { return value.name; }, object);
                content << L"• " << utf8_to_wide(name) << L"  (#" << id << L")\n";
                if (++shown == 20)
                    break;
            }
            if (snapshot->object_count() == 0)
                content << (is_vi ? L"Tài liệu chưa có đối tượng." : L"The document is empty.");
            else if (snapshot->object_count() > shown)
                content << L"…";
            RECT body = { insp_x + 14, content_y + 28, w - 14, h - status_h - 8 };
            const auto text = content.str();
            DrawTextW(mem_dc, text.c_str(), -1, &body, DT_LEFT | DT_NOPREFIX);
        } else {
            DrawTextW(mem_dc, is_vi ? L"LỊCH SỬ LỆNH" : L"COMMAND HISTORY", -1,
                      &rc_head, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
            content << L"Revision: " << snapshot->revision() << L"\n"
                    << (root().document().is_dirty()
                            ? (is_vi ? L"● Có thay đổi chưa lưu\n" : L"● Unsaved changes\n")
                            : (is_vi ? L"○ Đã lưu\n" : L"○ Saved\n"))
                    << (root().document().can_undo()
                            ? (is_vi ? L"● Có thể hoàn tác\n" : L"● Undo available\n")
                            : (is_vi ? L"○ Không có lệnh Undo\n" : L"○ Nothing to undo\n"))
                    << (root().document().can_redo()
                            ? (is_vi ? L"● Có thể làm lại" : L"● Redo available")
                            : (is_vi ? L"○ Không có lệnh Redo" : L"○ Nothing to redo"));
            RECT body = { insp_x + 14, content_y + 28, w - 14, content_y + 220 };
            const auto text = content.str();
            DrawTextW(mem_dc, text.c_str(), -1, &body, DT_LEFT | DT_NOPREFIX);
        }

        // Hairline borders between layout blocks
        HPEN pen_bdr = CreatePen(PS_SOLID, 1, col_border);
        HGDIOBJ old_bdr = SelectObject(mem_dc, pen_bdr);

        MoveToEx(mem_dc, 0, content_top, NULL); LineTo(mem_dc, w, content_top);
        MoveToEx(mem_dc, tool_w, content_top, NULL); LineTo(mem_dc, tool_w, h - status_h);
        MoveToEx(mem_dc, insp_x, content_top, NULL); LineTo(mem_dc, insp_x, h - status_h);

        SelectObject(mem_dc, old_bdr);
        DeleteObject(pen_bdr);
    }

    // 3. Topbar (Always on top)
    HBRUSH br_topbar = CreateSolidBrush(col_header);
    RECT rc_topbar = { 0, 0, w, topbar_h };
    FillRect(mem_dc, &rc_topbar, br_topbar);
    DeleteObject(br_topbar);

    // Hairline border under topbar
    HPEN pen_bdr_tb = CreatePen(PS_SOLID, 1, col_border);
    HGDIOBJ old_bdr_tb = SelectObject(mem_dc, pen_bdr_tb);
    MoveToEx(mem_dc, 0, topbar_h, NULL); LineTo(mem_dc, w, topbar_h);
    SelectObject(mem_dc, old_bdr_tb);
    DeleteObject(pen_bdr_tb);

    // Logo PK badge (bo nhẹ 6px)
    HBRUSH br_logo = CreateSolidBrush(col_teal);
    HPEN pen_logo = CreatePen(PS_SOLID, 1, col_teal);
    SelectObject(mem_dc, pen_logo);
    SelectObject(mem_dc, br_logo);
    RoundRect(mem_dc, 10, 6, 36, 32, 6, 6);
    DeleteObject(pen_logo);
    DeleteObject(br_logo);
    SelectObject(mem_dc, font_be_vietnam_bold);
    SetTextColor(mem_dc, is_dark ? RGB(7, 11, 16) : RGB(255, 255, 255));
    RECT rc_logo = { 10, 6, 36, 32 };
    DrawTextW(mem_dc, L"PK", -1, &rc_logo, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    // App brand title
    SetTextColor(mem_dc, col_text_white);
    RECT rc_app_title = { 42, 6, 145, 32 };
    DrawTextW(mem_dc, L"PikDraw PRO", -1, &rc_app_title, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    paint_main_menu_bar(mem_dc, w, topbar_h);

    // 7. Bottom Status Bar (Always at bottom)
    HBRUSH br_status = CreateSolidBrush(col_statusbar);
    RECT rc_status = { 0, h - status_h, w, h };
    FillRect(mem_dc, &rc_status, br_status);
    DeleteObject(br_status);

    HPEN pen_st_bdr = CreatePen(PS_SOLID, 1, col_border);
    HGDIOBJ old_st_bdr = SelectObject(mem_dc, pen_st_bdr);
    MoveToEx(mem_dc, 0, h - status_h, NULL); LineTo(mem_dc, w, h - status_h);
    SelectObject(mem_dc, old_st_bdr);
    DeleteObject(pen_st_bdr);

    // Live coordinates
    SelectObject(mem_dc, font_mono);
    SetTextColor(mem_dc, col_text_muted);
    std::wostringstream wcoord;
    wcoord << L"X: " << std::fixed << std::setprecision(1) << cursor_mm_x_ << L" mm   "
           << L"Y: " << std::fixed << std::setprecision(1) << cursor_mm_y_ << L" mm";
    std::wstring coord_str = wcoord.str();
    RECT rc_st_coord = { 16, h - status_h, 210, h };
    DrawTextW(mem_dc, coord_str.c_str(), -1, &rc_st_coord, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    // Theme Mode Badge Chip (bo nhẹ 5px)
    SelectObject(mem_dc, font_be_vietnam);
    HBRUSH br_tbadge = CreateSolidBrush(col_btn_bg);
    HPEN p_tb = CreatePen(PS_SOLID, 1, col_btn_bdr);
    SelectObject(mem_dc, p_tb);
    SelectObject(mem_dc, br_tbadge);
    RoundRect(mem_dc, 215, h - status_h + 3, 400, h - 3, 5, 5);
    DeleteObject(p_tb);
    DeleteObject(br_tbadge);
    SetTextColor(mem_dc, col_teal);
    const wchar_t* thm_badge = is_dark ? L"Theme: Dark Mode (Obsidian)" : L"Theme: Light Mode (Silver)";
    RECT rc_tbadge = { 215, h - status_h + 3, 400, h - 3 };
    DrawTextW(mem_dc, thm_badge, -1, &rc_tbadge, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    // Live status message
    SetTextColor(mem_dc, col_text_white);
    std::wstring wmsg = utf8_to_wide(status_msg_);
    RECT rc_st_msg = { 410, h - status_h, w - 230, h };
    DrawTextW(mem_dc, wmsg.c_str(), -1, &rc_st_msg, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    // Zoom buttons [-] 100% [+] (bo nhẹ 5px)
    SelectObject(mem_dc, font_mono);
    SetTextColor(mem_dc, col_text_white);
    std::wstring wzoom = std::to_wstring(zoom_level_) + L"%";
    RECT rc_zm_fit = { w - 210, h - status_h + 3, w - 145, h - 3 };
    RECT rc_zm_btn_minus = { w - 120, h - status_h + 3, w - 95, h - 3 };
    RECT rc_zm_txt = { w - 90, h - status_h, w - 50, h };
    RECT rc_zm_btn_plus = { w - 45, h - status_h + 3, w - 20, h - 3 };

    HBRUSH br_zm_btn = CreateSolidBrush(col_btn_bg);
    HPEN p_zm = CreatePen(PS_SOLID, 1, col_btn_bdr);
    SelectObject(mem_dc, p_zm);
    SelectObject(mem_dc, br_zm_btn);
    RoundRect(mem_dc, rc_zm_fit.left, rc_zm_fit.top, rc_zm_fit.right, rc_zm_fit.bottom, 5, 5);
    RoundRect(mem_dc, rc_zm_btn_minus.left, rc_zm_btn_minus.top, rc_zm_btn_minus.right, rc_zm_btn_minus.bottom, 5, 5);
    RoundRect(mem_dc, rc_zm_btn_plus.left, rc_zm_btn_plus.top, rc_zm_btn_plus.right, rc_zm_btn_plus.bottom, 5, 5);
    DeleteObject(p_zm);
    DeleteObject(br_zm_btn);

    DrawTextW(mem_dc, L"-", -1, &rc_zm_btn_minus, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    DrawTextW(mem_dc, is_vi ? L"Vừa trang" : L"Fit page", -1, &rc_zm_fit,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    DrawTextW(mem_dc, wzoom.c_str(), -1, &rc_zm_txt, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    DrawTextW(mem_dc, L"+", -1, &rc_zm_btn_plus, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    // 8. Paint Settings Modal Dialog if open
    if (show_settings_modal_) {
        paint_settings_modal(mem_dc, w, h);
    }

    // 8b. Paint New Document Modal Dialog if open
    if (show_new_doc_modal_) {
        paint_new_document_modal(mem_dc, w, h);
    }

    // 9. Paint the selected main-menu dropdown.
    if (open_menu_id_ >= 0) {
        const RECT anchor = get_menu_bar_item_rect(open_menu_id_, topbar_h);
        paint_dropdown_menu(mem_dc, open_menu_id_, anchor.left, topbar_h);
    }

    // 9b. Paint Page Preset Dropdown List if open (CorelDRAW / Illustrator Reference)
    if (show_page_preset_dropdown_) {
        paint_page_preset_dropdown(mem_dc, 52, topbar_h + tabbar_h + ribbon_h, kPagePresetDropdownWidth);
    }

    // 10. Paint Floating Tooltip Popover (Always topmost)
    if (tooltip_.visible && !tooltip_.text.empty()) {
        SelectObject(mem_dc, font_secondary ? font_secondary : font_be_vietnam);
        RECT calc_rc = { 0, 0, 0, 0 };
        DrawTextW(mem_dc, tooltip_.text.c_str(), -1, &calc_rc, DT_CALCRECT | DT_NOPREFIX);

        int tip_pad_x = 10;
        int tip_pad_y = 6;
        int tip_w = (calc_rc.right - calc_rc.left) + tip_pad_x * 2;
        int tip_h = (calc_rc.bottom - calc_rc.top) + tip_pad_y * 2;

        int tip_x = tooltip_.x + 12;
        int tip_y = tooltip_.y + 18;

        if (tip_x + tip_w > w - 8) {
            tip_x = w - tip_w - 8;
        }
        if (tip_x < 8) tip_x = 8;

        if (tip_y + tip_h > h - 28) {
            tip_y = tooltip_.y - tip_h - 6;
        }
        if (tip_y < 8) tip_y = 8;

        // Drop shadow
        RECT rc_shadow = { tip_x + 2, tip_y + 2, tip_x + tip_w + 2, tip_y + tip_h + 2 };
        HBRUSH br_shadow = CreateSolidBrush(RGB(15, 23, 42));
        HPEN pen_shadow = CreatePen(PS_SOLID, 1, RGB(15, 23, 42));
        HGDIOBJ old_psh = SelectObject(mem_dc, pen_shadow);
        HGDIOBJ old_bsh = SelectObject(mem_dc, br_shadow);
        RoundRect(mem_dc, rc_shadow.left, rc_shadow.top, rc_shadow.right, rc_shadow.bottom, 6, 6);
        SelectObject(mem_dc, old_psh);
        SelectObject(mem_dc, old_bsh);
        DeleteObject(pen_shadow);
        DeleteObject(br_shadow);

        // Tooltip container (Obsidian Dark with Cyber Mint glow border)
        COLORREF col_tip_bg = RGB(24, 24, 27);
        COLORREF col_tip_bdr = RGB(0, 242, 195);
        HBRUSH br_tip = CreateSolidBrush(col_tip_bg);
        HPEN pen_tip = CreatePen(PS_SOLID, 1, col_tip_bdr);
        HGDIOBJ old_pt = SelectObject(mem_dc, pen_tip);
        HGDIOBJ old_bt = SelectObject(mem_dc, br_tip);
        RoundRect(mem_dc, tip_x, tip_y, tip_x + tip_w, tip_y + tip_h, 6, 6);
        SelectObject(mem_dc, old_pt);
        SelectObject(mem_dc, old_bt);
        DeleteObject(pen_tip);
        DeleteObject(br_tip);

        // Text inside tooltip
        RECT rc_text = { tip_x + tip_pad_x, tip_y + tip_pad_y, tip_x + tip_w - tip_pad_x, tip_y + tip_h - tip_pad_y };
        SetBkMode(mem_dc, TRANSPARENT);
        SetTextColor(mem_dc, RGB(255, 255, 255));
        DrawTextW(mem_dc, tooltip_.text.c_str(), -1, &rc_text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }

    BitBlt(hdc, 0, 0, w, h, mem_dc, 0, 0, SRCCOPY);

    SelectObject(mem_dc, old_font);
}

#endif

} // namespace pikdraw::desktop
