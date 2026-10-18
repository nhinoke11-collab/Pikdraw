#pragma once

#ifdef _WIN32
#include <windows.h>

namespace pikdraw::desktop {

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

} // namespace pikdraw::desktop
#endif
