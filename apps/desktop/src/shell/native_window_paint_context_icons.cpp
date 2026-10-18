#include "native_window_paint_icons.hpp"
#include "native_gdiplus_api.hpp"
#include <cmath>

namespace pikdraw::desktop {

#ifdef _WIN32

static void draw_object_context_icon_gdiplus(GdiplusApi* api, GpGraphics* g, ObjectContextAction action, float cx, float cy, COLORREF color) {
    GpPen* pen = nullptr;
    api->CreatePen1(MakeGdiplusARGB(color, 255), 1.5f, kUnitPixel, &pen);
    api->SetPenLineCap197819(pen, kLineCapRound, kLineCapRound, 0);
    api->SetPenLineJoin(pen, kLineJoinRound);

    GpSolidFill* brush = nullptr;
    api->CreateSolidFill(MakeGdiplusARGB(color, 255), &brush);

    GpPen* thin_pen = nullptr;
    api->CreatePen1(MakeGdiplusARGB(color, 160), 1.0f, kUnitPixel, &thin_pen);

    switch (action) {
    case ObjectContextAction::FlipHorizontal: {
        api->DrawLine(g, thin_pen, cx, cy - 7.5f, cx, cy + 7.5f);
        const GpPointF left_tri[3] = {{cx - 1.5f, cy - 5.5f}, {cx - 1.5f, cy + 5.5f}, {cx - 7.5f, cy}};
        api->FillPolygon(g, brush, left_tri, 3, kFillModeAlternate);
        const GpPointF right_tri[3] = {{cx + 1.5f, cy - 5.5f}, {cx + 1.5f, cy + 5.5f}, {cx + 7.5f, cy}};
        api->DrawLines(g, pen, right_tri, 3);
        api->DrawLine(g, pen, right_tri[2].X, right_tri[2].Y, right_tri[0].X, right_tri[0].Y);
        break;
    }
    case ObjectContextAction::FlipVertical: {
        api->DrawLine(g, thin_pen, cx - 7.5f, cy, cx + 7.5f, cy);
        const GpPointF top_tri[3] = {{cx - 5.5f, cy - 1.5f}, {cx + 5.5f, cy - 1.5f}, {cx, cy - 7.5f}};
        api->FillPolygon(g, brush, top_tri, 3, kFillModeAlternate);
        const GpPointF btm_tri[3] = {{cx - 5.5f, cy + 1.5f}, {cx + 5.5f, cy + 1.5f}, {cx, cy + 7.5f}};
        api->DrawLines(g, pen, btm_tri, 3);
        api->DrawLine(g, pen, btm_tri[2].X, btm_tri[2].Y, btm_tri[0].X, btm_tri[0].Y);
        break;
    }
    case ObjectContextAction::Rotate90CW: {
        GpPath* path = nullptr;
        api->CreatePath(kFillModeAlternate, &path);
        api->AddPathArc(path, cx - 6.0f, cy - 5.5f, 11.5f, 11.5f, 165.0f, 125.0f);
        api->DrawPath(g, pen, path);
        api->DeletePath(path);
        const GpPointF arrow[3] = {{cx + 3.0f, cy - 3.5f}, {cx + 7.0f, cy + 1.5f}, {cx + 1.5f, cy + 1.0f}};
        api->FillPolygon(g, brush, arrow, 3, kFillModeAlternate);
        break;
    }
    case ObjectContextAction::Rotate90CCW: {
        GpPath* path = nullptr;
        api->CreatePath(kFillModeAlternate, &path);
        api->AddPathArc(path, cx - 5.5f, cy - 5.5f, 11.5f, 11.5f, 15.0f, -125.0f);
        api->DrawPath(g, pen, path);
        api->DeletePath(path);
        const GpPointF arrow[3] = {{cx - 3.0f, cy - 3.5f}, {cx - 7.0f, cy + 1.5f}, {cx - 1.5f, cy + 1.0f}};
        api->FillPolygon(g, brush, arrow, 3, kFillModeAlternate);
        break;
    }
    case ObjectContextAction::AlignLeft:
        api->DrawLine(g, pen, cx - 7.0f, cy - 7.0f, cx - 7.0f, cy + 7.0f);
        api->DrawRectangle(g, pen, cx - 7.0f, cy - 5.0f, 11.0f, 4.0f);
        api->DrawRectangle(g, pen, cx - 7.0f, cy + 1.0f, 7.0f, 4.0f);
        break;
    case ObjectContextAction::AlignCenterHorizontal:
        api->DrawLine(g, thin_pen, cx, cy - 7.5f, cx, cy + 7.5f);
        api->DrawRectangle(g, pen, cx - 5.0f, cy - 5.0f, 10.0f, 4.0f);
        api->DrawRectangle(g, pen, cx - 3.0f, cy + 1.0f, 6.0f, 4.0f);
        break;
    case ObjectContextAction::AlignRight:
        api->DrawLine(g, pen, cx + 7.0f, cy - 7.0f, cx + 7.0f, cy + 7.0f);
        api->DrawRectangle(g, pen, cx - 4.0f, cy - 5.0f, 11.0f, 4.0f);
        api->DrawRectangle(g, pen, cx, cy + 1.0f, 7.0f, 4.0f);
        break;
    case ObjectContextAction::AlignTop:
        api->DrawLine(g, pen, cx - 7.0f, cy - 7.0f, cx + 7.0f, cy - 7.0f);
        api->DrawRectangle(g, pen, cx - 5.0f, cy - 7.0f, 4.0f, 11.0f);
        api->DrawRectangle(g, pen, cx + 1.0f, cy - 7.0f, 4.0f, 7.0f);
        break;
    case ObjectContextAction::AlignCenterVertical:
        api->DrawLine(g, thin_pen, cx - 7.5f, cy, cx + 7.5f, cy);
        api->DrawRectangle(g, pen, cx - 5.0f, cy - 5.0f, 4.0f, 10.0f);
        api->DrawRectangle(g, pen, cx + 1.0f, cy - 3.0f, 4.0f, 6.0f);
        break;
    case ObjectContextAction::AlignBottom:
        api->DrawLine(g, pen, cx - 7.0f, cy + 7.0f, cx + 7.0f, cy + 7.0f);
        api->DrawRectangle(g, pen, cx - 5.0f, cy - 4.0f, 4.0f, 11.0f);
        api->DrawRectangle(g, pen, cx + 1.0f, cy, 4.0f, 7.0f);
        break;
    case ObjectContextAction::CenterPage:
        api->DrawRectangle(g, pen, cx - 6.0f, cy - 8.0f, 12.0f, 16.0f);
        api->DrawLine(g, thin_pen, cx, cy - 4.0f, cx, cy + 4.0f);
        api->DrawLine(g, thin_pen, cx - 4.0f, cy, cx + 4.0f, cy);
        break;
    case ObjectContextAction::DistributeHorizontal:
        api->DrawRectangle(g, pen, cx - 8.0f, cy - 3.5f, 4.0f, 7.0f);
        api->DrawRectangle(g, pen, cx - 2.0f, cy - 3.5f, 4.0f, 7.0f);
        api->DrawRectangle(g, pen, cx + 4.0f, cy - 3.5f, 4.0f, 7.0f);
        break;
    case ObjectContextAction::DistributeVertical:
        api->DrawRectangle(g, pen, cx - 3.5f, cy - 8.0f, 7.0f, 4.0f);
        api->DrawRectangle(g, pen, cx - 3.5f, cy - 2.0f, 7.0f, 4.0f);
        api->DrawRectangle(g, pen, cx - 3.5f, cy + 4.0f, 7.0f, 4.0f);
        break;
    case ObjectContextAction::Group:
        api->DrawRectangle(g, pen, cx - 7.0f, cy - 5.0f, 8.0f, 7.0f);
        api->DrawRectangle(g, pen, cx - 1.0f, cy - 2.0f, 8.0f, 7.0f);
        break;
    case ObjectContextAction::BooleanUnion:
        api->DrawRectangle(g, pen, cx - 7.0f, cy - 5.0f, 8.0f, 7.0f);
        api->DrawRectangle(g, pen, cx - 1.0f, cy - 2.0f, 8.0f, 7.0f);
        api->DrawLine(g, pen, cx - 2.0f, cy, cx + 2.0f, cy);
        api->DrawLine(g, pen, cx, cy - 2.0f, cx, cy + 2.0f);
        break;
    case ObjectContextAction::BooleanDifference:
        api->DrawRectangle(g, pen, cx - 7.0f, cy - 5.0f, 8.0f, 7.0f);
        api->DrawRectangle(g, pen, cx - 1.0f, cy - 2.0f, 8.0f, 7.0f);
        api->DrawLine(g, pen, cx - 4.0f, cy, cx, cy);
        break;
    case ObjectContextAction::BooleanIntersection:
        api->DrawRectangle(g, pen, cx - 7.0f, cy - 5.0f, 8.0f, 7.0f);
        api->DrawRectangle(g, pen, cx - 1.0f, cy - 2.0f, 8.0f, 7.0f);
        api->FillRectangle(g, brush, cx - 1.0f, cy - 2.0f, 2.0f, 4.0f);
        break;
    case ObjectContextAction::BooleanExclude:
        api->DrawRectangle(g, pen, cx - 7.0f, cy - 5.0f, 8.0f, 7.0f);
        api->DrawRectangle(g, pen, cx - 1.0f, cy - 2.0f, 8.0f, 7.0f);
        api->DrawLine(g, pen, cx - 2.0f, cy - 2.0f, cx + 2.0f, cy + 2.0f);
        api->DrawLine(g, pen, cx + 2.0f, cy - 2.0f, cx - 2.0f, cy + 2.0f);
        break;
    case ObjectContextAction::BooleanReverseDifference:
        api->DrawRectangle(g, pen, cx - 7.0f, cy - 5.0f, 8.0f, 7.0f);
        api->DrawRectangle(g, pen, cx - 1.0f, cy - 2.0f, 8.0f, 7.0f);
        api->DrawLine(g, pen, cx, cy, cx + 4.0f, cy);
        break;
    }
    api->DeletePen(thin_pen);
    api->DeleteBrush(brush);
    api->DeletePen(pen);
}

void draw_object_context_icon(HDC dc, ObjectContextAction action, int cx, int cy, COLORREF color) {
    auto* api = GdiplusApi::get();
    if (api) {
        GpGraphics* g = nullptr;
        if (api->CreateFromHDC(dc, &g) == 0 && g) {
            api->SetSmoothingMode(g, kSmoothingModeAntiAlias);
            api->SetPixelOffsetMode(g, kPixelOffsetModeHalf);
            draw_object_context_icon_gdiplus(api, g, action, static_cast<float>(cx), static_cast<float>(cy), color);
            api->DeleteGraphics(g);
            return;
        }
    }
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
        line(cx, cy - 4, cx, cy + 4);
        line(cx - 4, cy, cx + 4, cy);
        break;
    case ObjectContextAction::DistributeHorizontal:
        box(cx - 8, cy - 3, cx - 4, cy + 3);
        box(cx - 2, cy - 3, cx + 2, cy + 3);
        box(cx + 4, cy - 3, cx + 8, cy + 3);
        break;
    case ObjectContextAction::DistributeVertical:
        box(cx - 3, cy - 8, cx + 3, cy - 4);
        box(cx - 3, cy - 2, cx + 3, cy + 2);
        box(cx - 3, cy + 4, cx + 3, cy + 8);
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
        line(cx - 4, cy, cx, cy);
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
        line(cx, cy, cx + 4, cy);
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

void draw_lock_icon(HDC dc, int cx, int cy, bool locked, COLORREF color) {
    auto* api = GdiplusApi::get();
    if (api) {
        GpGraphics* g = nullptr;
        if (api->CreateFromHDC(dc, &g) == 0 && g) {
            api->SetSmoothingMode(g, kSmoothingModeAntiAlias);
            api->SetPixelOffsetMode(g, kPixelOffsetModeHalf);
            const float fcx = static_cast<float>(cx);
            const float fcy = static_cast<float>(cy);

            GpPen* pen = nullptr;
            api->CreatePen1(MakeGdiplusARGB(color, 255), 1.5f, kUnitPixel, &pen);
            api->SetPenLineCap197819(pen, kLineCapRound, kLineCapRound, 0);

            GpSolidFill* brush = nullptr;
            api->CreateSolidFill(MakeGdiplusARGB(color, 255), &brush);

            if (locked) {
                GpPath* shackle = nullptr;
                api->CreatePath(kFillModeAlternate, &shackle);
                api->AddPathArc(shackle, fcx - 3.5f, fcy - 7.5f, 7.0f, 7.0f, 180.0f, 180.0f);
                api->AddPathLine(shackle, fcx + 3.5f, fcy - 4.0f, fcx + 3.5f, fcy - 1.0f);
                api->AddPathLine(shackle, fcx - 3.5f, fcy - 1.0f, fcx - 3.5f, fcy - 4.0f);
                api->DrawPath(g, pen, shackle);
                api->DeletePath(shackle);

                api->FillRectangle(g, brush, fcx - 5.0f, fcy - 1.0f, 10.0f, 8.0f);
                GpSolidFill* hole = nullptr;
                api->CreateSolidFill(MakeGdiplusARGB(RGB(255, 255, 255), 230), &hole);
                api->FillEllipse(g, hole, fcx - 1.2f, fcy + 1.8f, 2.4f, 2.4f);
                api->DeleteBrush(hole);
            } else {
                GpPath* shackle = nullptr;
                api->CreatePath(kFillModeAlternate, &shackle);
                api->AddPathArc(shackle, fcx - 4.5f, fcy - 9.5f, 7.0f, 7.0f, 180.0f, 180.0f);
                api->AddPathLine(shackle, fcx - 4.5f, fcy - 6.0f, fcx - 4.5f, fcy - 1.0f);
                api->DrawPath(g, pen, shackle);
                api->DeletePath(shackle);

                api->FillRectangle(g, brush, fcx - 5.0f, fcy - 1.0f, 10.0f, 8.0f);
                GpSolidFill* hole = nullptr;
                api->CreateSolidFill(MakeGdiplusARGB(RGB(255, 255, 255), 230), &hole);
                api->FillEllipse(g, hole, fcx - 1.2f, fcy + 1.8f, 2.4f, 2.4f);
                api->DeleteBrush(hole);
            }
            api->DeleteBrush(brush);
            api->DeletePen(pen);
            api->DeleteGraphics(g);
            return;
        }
    }
    HPEN pen = CreatePen(PS_SOLID, 1, color);
    HBRUSH brush = CreateSolidBrush(color);
    const HGDIOBJ old_pen = SelectObject(dc, pen);
    const HGDIOBJ old_brush = SelectObject(dc, brush);
    Rectangle(dc, cx - 5, cy - 1, cx + 5, cy + 7);
    Arc(dc, cx - 4, cy - 7, cx + 4, cy + 1, cx + 4, cy - 1, cx - 4, cy - 1);
    SelectObject(dc, old_brush);
    SelectObject(dc, old_pen);
    DeleteObject(brush);
    DeleteObject(pen);
}

void draw_angle_icon(HDC dc, int cx, int cy, COLORREF color) {
    auto* api = GdiplusApi::get();
    if (api) {
        GpGraphics* g = nullptr;
        if (api->CreateFromHDC(dc, &g) == 0 && g) {
            api->SetSmoothingMode(g, kSmoothingModeAntiAlias);
            api->SetPixelOffsetMode(g, kPixelOffsetModeHalf);
            const float fcx = static_cast<float>(cx);
            const float fcy = static_cast<float>(cy);

            GpPen* pen = nullptr;
            api->CreatePen1(MakeGdiplusARGB(color, 255), 1.5f, kUnitPixel, &pen);
            api->SetPenLineCap197819(pen, kLineCapRound, kLineCapRound, 0);

            api->DrawLine(g, pen, fcx - 6.0f, fcy + 5.0f, fcx + 6.0f, fcy + 5.0f);
            api->DrawLine(g, pen, fcx - 6.0f, fcy + 5.0f, fcx + 3.0f, fcy - 4.0f);

            GpPath* arc = nullptr;
            api->CreatePath(kFillModeAlternate, &arc);
            api->AddPathArc(arc, fcx - 11.0f, fcy, 10.0f, 10.0f, 315.0f, 45.0f);
            api->DrawPath(g, pen, arc);
            api->DeletePath(arc);

            api->DeletePen(pen);
            api->DeleteGraphics(g);
            return;
        }
    }
    HPEN pen = CreatePen(PS_SOLID, 1, color);
    const HGDIOBJ old_pen = SelectObject(dc, pen);
    MoveToEx(dc, cx - 6, cy + 5, nullptr);
    LineTo(dc, cx + 6, cy + 5);
    MoveToEx(dc, cx - 6, cy + 5, nullptr);
    LineTo(dc, cx + 3, cy - 4);
    SelectObject(dc, old_pen);
    DeleteObject(pen);
}

#endif

} // namespace pikdraw::desktop
