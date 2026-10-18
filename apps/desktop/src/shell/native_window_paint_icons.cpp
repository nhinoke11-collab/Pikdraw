#include "native_window_paint_icons.hpp"
#include <cmath>
#include <numbers>

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
    } else if (tool == 13) {
        // Tool 13: Gradient: ramp rectangle with vector guide line and stop handles
        api->DrawRectangle(g, pen, cx - 7.5f, cy - 6.5f, 15.0f, 13.0f);
        api->DrawLine(g, pen, cx - 6.0f, cy + 4.5f, cx + 6.0f, cy - 4.5f);
        api->FillEllipse(g, fill_brush, cx - 7.5f, cy + 3.0f, 3.5f, 3.5f);
        api->FillEllipse(g, fill_brush, cx + 4.0f, cy - 6.0f, 3.5f, 3.5f);
        api->FillEllipse(g, fill_brush, cx - 1.2f, cy - 1.2f, 2.4f, 2.4f);
    } else if (tool == 14) {
        // Tool 14: Contour: concentric inset and outset perimeter boundary lines
        api->DrawRectangle(g, pen, cx - 7.5f, cy - 7.5f, 15.0f, 15.0f);
        GpPen* inner_pen = nullptr;
        api->CreatePen1(MakeGdiplusARGB(color, 160), 1.0f, kUnitPixel, &inner_pen);
        api->DrawRectangle(g, inner_pen, cx - 3.5f, cy - 3.5f, 7.0f, 7.0f);
        api->DeletePen(inner_pen);
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
    } else if (tool == 13) {
        Rectangle(dc, center_x - 7, center_y - 6, center_x + 8, center_y + 7);
        MoveToEx(dc, center_x - 6, center_y + 4, nullptr);
        LineTo(dc, center_x + 6, center_y - 4);
        Ellipse(dc, center_x - 7, center_y + 2, center_x - 4, center_y + 5);
        Ellipse(dc, center_x + 4, center_y - 5, center_x + 7, center_y - 2);
    } else if (tool == 14) {
        Rectangle(dc, center_x - 7, center_y - 7, center_x + 8, center_y + 8);
        Rectangle(dc, center_x - 3, center_y - 3, center_x + 4, center_y + 4);
    }
    SelectObject(dc, old_brush);
    SelectObject(dc, old_pen);
    DeleteObject(pen);
}
} // namespace

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

#endif

} // namespace pikdraw::desktop
