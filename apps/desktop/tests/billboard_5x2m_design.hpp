#pragma once

inline pikdraw::automation::JsonValue make_rgb(double r, double g, double b, double a = 1.0) {
    return pikdraw::automation::JsonValue::Object{
        {"model", "rgb"},
        {"channels", pikdraw::automation::JsonValue::Object{
            {"r", r}, {"g", g}, {"b", b}, {"alpha", a}
        }}
    };
}

inline pikdraw::automation::JsonValue make_point(double x, double y) {
    return pikdraw::automation::JsonValue::Object{
        {"x_mm", x}, {"y_mm", y}
    };
}

inline pikdraw::automation::JsonValue::Array build_billboard_5x2m_operations() {
    using namespace pikdraw::automation;
    JsonValue::Array ops;

    // 1. Cột 1/3 bên trái (Brand Identity): Nền màu Deep Navy
    ops.emplace_back(JsonValue::Object{
        {"op", "object.create_rectangle"}, {"client_ref", "bg_left"},
        {"name", "Nền cột thương hiệu (1/3)"},
        {"x_mm", 0.0}, {"y_mm", 0.0}, {"width_mm", 1666.0}, {"height_mm", 2000.0},
        {"fill", make_rgb(0.02, 0.04, 0.09, 1.0)},
        {"stroke", make_rgb(0.02, 0.04, 0.09, 1.0)}, {"stroke_width_mm", 0.0}
    });

    // 2. Cột 2/3 bên phải (Main Content): Nền Slate hiện đại
    ops.emplace_back(JsonValue::Object{
        {"op", "object.create_rectangle"}, {"client_ref", "bg_right"},
        {"name", "Nền cột nội dung (2/3)"},
        {"x_mm", 1666.0}, {"y_mm", 0.0}, {"width_mm", 3334.0}, {"height_mm", 2000.0},
        {"fill", make_rgb(0.06, 0.09, 0.16, 1.0)},
        {"stroke", make_rgb(0.06, 0.09, 0.16, 1.0)}, {"stroke_width_mm", 0.0}
    });

    // 3. Đường phân cách dọc 1/3: Vàng ánh kim Gold
    ops.emplace_back(JsonValue::Object{
        {"op", "object.create_rectangle"}, {"client_ref", "divider_gold"},
        {"name", "Thanh phân cách 1/3 Gold"},
        {"x_mm", 1656.0}, {"y_mm", 0.0}, {"width_mm", 10.0}, {"height_mm", 2000.0},
        {"fill", make_rgb(0.96, 0.62, 0.04, 1.0)},
        {"stroke", make_rgb(0.96, 0.62, 0.04, 1.0)}, {"stroke_width_mm", 0.0}
    });

    // 4. Khung viền huy hiệu thương hiệu cột 1/3
    ops.emplace_back(JsonValue::Object{
        {"op", "object.create_rectangle"}, {"client_ref", "brand_frame"},
        {"name", "Khung viền nhận diện thương hiệu"},
        {"x_mm", 183.0}, {"y_mm", 150.0}, {"width_mm", 1300.0}, {"height_mm", 1700.0},
        {"fill", make_rgb(0.04, 0.07, 0.15, 1.0)},
        {"stroke", make_rgb(0.85, 0.47, 0.02, 1.0)}, {"stroke_width_mm", 6.0}
    });

    // 5. Logo Octagon Crest (Polyline khép kín)
    ops.emplace_back(JsonValue::Object{
        {"op", "object.create_polyline"}, {"client_ref", "logo_badge"},
        {"name", "Huy hiệu Logo Octagon"},
        {"points_mm", JsonValue::Array{
            make_point(833.0, 360.0), make_point(990.0, 425.0),
            make_point(1055.0, 580.0), make_point(990.0, 735.0),
            make_point(833.0, 800.0), make_point(676.0, 735.0),
            make_point(611.0, 580.0), make_point(676.0, 425.0)
        }},
        {"closed", true},
        {"fill", make_rgb(0.12, 0.11, 0.29, 1.0)},
        {"stroke", make_rgb(0.96, 0.62, 0.04, 1.0)}, {"stroke_width_mm", 5.0}
    });

    // 6. Logo Ngôi sao 8 cánh hình học vector (Polyline khép kín)
    ops.emplace_back(JsonValue::Object{
        {"op", "object.create_polyline"}, {"client_ref", "logo_star"},
        {"name", "Ngôi sao 8 cánh biểu tượng Logo"},
        {"points_mm", JsonValue::Array{
            make_point(833.0, 430.0), make_point(882.0, 531.0),
            make_point(983.0, 580.0), make_point(882.0, 629.0),
            make_point(833.0, 730.0), make_point(784.0, 629.0),
            make_point(683.0, 580.0), make_point(784.0, 531.0)
        }},
        {"closed", true},
        {"fill", make_rgb(0.98, 0.75, 0.14, 1.0)},
        {"stroke", make_rgb(0.71, 0.33, 0.04, 1.0)}, {"stroke_width_mm", 3.0}
    });

    // 7. Tâm đá quý đỏ Logo (Ellipse)
    ops.emplace_back(JsonValue::Object{
        {"op", "object.create_ellipse"}, {"client_ref", "logo_core"},
        {"name", "Tâm đá quý Logo"},
        {"x_mm", 803.0}, {"y_mm", 550.0}, {"width_mm", 60.0}, {"height_mm", 60.0},
        {"fill", make_rgb(0.88, 0.11, 0.28, 1.0)},
        {"stroke", make_rgb(0.99, 0.94, 0.54, 1.0)}, {"stroke_width_mm", 2.0}
    });

    // 8. Tên thương hiệu Logo: HOÀNG GIA
    ops.emplace_back(JsonValue::Object{
        {"op", "object.create_text"}, {"client_ref", "brand_title"},
        {"name", "Tên thương hiệu Logo"},
        {"text", "HOÀNG GIA"}, {"font_family", "Segoe UI"},
        {"font_weight", 900.0}, {"font_size_pt", 52.0}, {"alignment", "center"},
        {"x_mm", 833.0}, {"y_mm", 910.0},
        {"fill", make_rgb(0.99, 0.83, 0.30, 1.0)}
    });

    // 9. Phụ đề Logo: LUXURY INTERIOR
    ops.emplace_back(JsonValue::Object{
        {"op", "object.create_text"}, {"client_ref", "brand_subtitle"},
        {"name", "Phụ đề thương hiệu"},
        {"text", "LUXURY INTERIOR"}, {"font_family", "Segoe UI"},
        {"font_weight", 700.0}, {"font_size_pt", 22.0}, {"alignment", "center"},
        {"x_mm", 833.0}, {"y_mm", 980.0},
        {"fill", make_rgb(1.0, 1.0, 1.0, 1.0)}
    });

    // 10. Đường kẻ phân cách thương hiệu (Line)
    ops.emplace_back(JsonValue::Object{
        {"op", "object.create_line"}, {"client_ref", "brand_line"},
        {"name", "Đường kẻ phân cách thương hiệu"},
        {"points_mm", JsonValue::Array{make_point(500.0, 1030.0), make_point(1166.0, 1030.0)}},
        {"stroke", make_rgb(0.96, 0.62, 0.04, 1.0)}, {"stroke_width_mm", 4.0}
    });

    // 11. Slogan thương hiệu: ĐẲNG CẤP KHÔNG GIAN SỐNG
    ops.emplace_back(JsonValue::Object{
        {"op", "object.create_text"}, {"client_ref", "brand_slogan"},
        {"name", "Khẩu hiệu Slogan thương hiệu"},
        {"text", "ĐẲNG CẤP KHÔNG GIAN SỐNG"}, {"font_family", "Segoe UI"},
        {"font_weight", 600.0}, {"font_size_pt", 20.0}, {"alignment", "center"},
        {"x_mm", 833.0}, {"y_mm", 1110.0},
        {"fill", make_rgb(0.73, 0.90, 0.99, 1.0)}
    });

    // 12. Khung chứng nhận ISO 9001 (Rectangle)
    ops.emplace_back(JsonValue::Object{
        {"op", "object.create_rectangle"}, {"client_ref", "brand_iso_box"},
        {"name", "Khung chứng nhận ISO"},
        {"x_mm", 433.0}, {"y_mm", 1440.0}, {"width_mm", 800.0}, {"height_mm", 280.0},
        {"fill", make_rgb(0.01, 0.02, 0.08, 1.0)},
        {"stroke", make_rgb(0.96, 0.62, 0.04, 1.0)}, {"stroke_width_mm", 3.0}
    });

    // 13. Tiêu đề ISO 9001
    ops.emplace_back(JsonValue::Object{
        {"op", "object.create_text"}, {"client_ref", "brand_iso_title"},
        {"name", "Tiêu chuẩn ISO"},
        {"text", "ISO 9001:2015"}, {"font_family", "Segoe UI"},
        {"font_weight", 800.0}, {"font_size_pt", 24.0}, {"alignment", "center"},
        {"x_mm", 833.0}, {"y_mm", 1540.0},
        {"fill", make_rgb(0.99, 0.83, 0.30, 1.0)}
    });

    // 14. Phụ đề tiêu chuẩn quốc tế
    ops.emplace_back(JsonValue::Object{
        {"op", "object.create_text"}, {"client_ref", "brand_iso_sub"},
        {"name", "Phụ đề tiêu chuẩn ISO"},
        {"text", "TIÊU CHUẨN CHẤT LƯỢNG QUỐC TẾ"}, {"font_family", "Segoe UI"},
        {"font_weight", 600.0}, {"font_size_pt", 15.0}, {"alignment", "center"},
        {"x_mm", 833.0}, {"y_mm", 1630.0},
        {"fill", make_rgb(1.0, 1.0, 1.0, 1.0)}
    });

    // --- Cột 2/3 bên phải: Thông tin chính và nội dung ---

    // 15. Dải băng trang trí trên (Top Ribbon)
    ops.emplace_back(JsonValue::Object{
        {"op", "object.create_rectangle"}, {"client_ref", "top_ribbon"},
        {"name", "Dải băng trang trí trên"},
        {"x_mm", 1666.0}, {"y_mm", 0.0}, {"width_mm", 3334.0}, {"height_mm", 35.0},
        {"fill", make_rgb(0.01, 0.52, 0.78, 1.0)},
        {"stroke", make_rgb(0.01, 0.52, 0.78, 1.0)}, {"stroke_width_mm", 0.0}
    });

    // 16. Khung danh mục hệ thống
    ops.emplace_back(JsonValue::Object{
        {"op", "object.create_rectangle"}, {"client_ref", "category_box"},
        {"name", "Khung danh mục dịch vụ"},
        {"x_mm", 1800.0}, {"y_mm", 110.0}, {"width_mm", 1850.0}, {"height_mm", 90.0},
        {"fill", make_rgb(0.12, 0.16, 0.23, 1.0)},
        {"stroke", make_rgb(0.96, 0.62, 0.04, 1.0)}, {"stroke_width_mm", 2.5}
    });

    // 17. Chữ danh mục hệ thống
    ops.emplace_back(JsonValue::Object{
        {"op", "object.create_text"}, {"client_ref", "category_text"},
        {"name", "Chữ danh mục dịch vụ"},
        {"text", "HỆ THỐNG PHÂN PHỐI & THI CÔNG NỘI THẤT CAO CẤP"},
        {"font_family", "Segoe UI"}, {"font_weight", 700.0}, {"font_size_pt", 20.0},
        {"alignment", "left"}, {"x_mm", 1840.0}, {"y_mm", 170.0},
        {"fill", make_rgb(0.99, 0.83, 0.30, 1.0)}
    });

    // 18. Tên khách hàng / Đơn vị lớn: TỔNG KHO NỘI THẤT HOÀNG GIA
    ops.emplace_back(JsonValue::Object{
        {"op", "object.create_text"}, {"client_ref", "main_title"},
        {"name", "Tên đơn vị khách hàng chính"},
        {"text", "TỔNG KHO NỘI THẤT HOÀNG GIA"},
        {"font_family", "Segoe UI"}, {"font_weight", 900.0}, {"font_size_pt", 68.0},
        {"alignment", "left"}, {"x_mm", 1800.0}, {"y_mm", 350.0},
        {"fill", make_rgb(1.0, 1.0, 1.0, 1.0)}
    });

    // 19. Dòng sản phẩm chủ lực
    ops.emplace_back(JsonValue::Object{
        {"op", "object.create_text"}, {"client_ref", "products_headline"},
        {"name", "Dòng sản phẩm chủ lực"},
        {"text", "SOFA DA Ý - BÀN ĂN MẶT ĐÁ - TỦ BẾP & GIƯỜNG GỖ TỰ NHIÊN"},
        {"font_family", "Segoe UI"}, {"font_weight", 700.0}, {"font_size_pt", 28.0},
        {"alignment", "left"}, {"x_mm", 1800.0}, {"y_mm", 470.0},
        {"fill", make_rgb(0.22, 0.74, 0.97, 1.0)}
    });

    // 20. Đường kẻ phân cách nội dung (Line)
    ops.emplace_back(JsonValue::Object{
        {"op", "object.create_line"}, {"client_ref", "content_divider"},
        {"name", "Đường kẻ phân cách nội dung"},
        {"points_mm", JsonValue::Array{make_point(1800.0, 540.0), make_point(4850.0, 540.0)}},
        {"stroke", make_rgb(0.20, 0.25, 0.33, 1.0)}, {"stroke_width_mm", 3.0}
    });

    // 21. Khung cam kết 1: 100% GỖ TỰ NHIÊN
    ops.emplace_back(JsonValue::Object{
        {"op", "object.create_rectangle"}, {"client_ref", "pillar_1_box"},
        {"name", "Khung cam kết 1"},
        {"x_mm", 1800.0}, {"y_mm", 600.0}, {"width_mm", 950.0}, {"height_mm", 260.0},
        {"fill", make_rgb(0.12, 0.16, 0.23, 1.0)},
        {"stroke", make_rgb(0.22, 0.74, 0.97, 1.0)}, {"stroke_width_mm", 3.0}
    });
    ops.emplace_back(JsonValue::Object{
        {"op", "object.create_text"}, {"client_ref", "pillar_1_text"},
        {"name", "Tiêu đề cam kết 1"},
        {"text", "100% GỖ TỰ NHIÊN"}, {"font_family", "Segoe UI"},
        {"font_weight", 800.0}, {"font_size_pt", 24.0}, {"alignment", "left"},
        {"x_mm", 1850.0}, {"y_mm", 690.0},
        {"fill", make_rgb(1.0, 1.0, 1.0, 1.0)}
    });
    ops.emplace_back(JsonValue::Object{
        {"op", "object.create_text"}, {"client_ref", "pillar_1_sub"},
        {"name", "Mô tả cam kết 1"},
        {"text", "Gỗ óc chó & gõ đỏ nhập khẩu"}, {"font_family", "Segoe UI"},
        {"font_weight", 500.0}, {"font_size_pt", 16.0}, {"alignment", "left"},
        {"x_mm", 1850.0}, {"y_mm", 770.0},
        {"fill", make_rgb(0.73, 0.90, 0.99, 1.0)}
    });

    // 22. Khung cam kết 2: BẢO HÀNH 10 NĂM
    ops.emplace_back(JsonValue::Object{
        {"op", "object.create_rectangle"}, {"client_ref", "pillar_2_box"},
        {"name", "Khung cam kết 2"},
        {"x_mm", 2850.0}, {"y_mm", 600.0}, {"width_mm", 950.0}, {"height_mm", 260.0},
        {"fill", make_rgb(0.12, 0.16, 0.23, 1.0)},
        {"stroke", make_rgb(0.96, 0.62, 0.04, 1.0)}, {"stroke_width_mm", 3.0}
    });
    ops.emplace_back(JsonValue::Object{
        {"op", "object.create_text"}, {"client_ref", "pillar_2_text"},
        {"name", "Tiêu đề cam kết 2"},
        {"text", "BẢO HÀNH 10 NĂM"}, {"font_family", "Segoe UI"},
        {"font_weight", 800.0}, {"font_size_pt", 24.0}, {"alignment", "left"},
        {"x_mm", 2900.0}, {"y_mm", 690.0},
        {"fill", make_rgb(0.99, 0.83, 0.30, 1.0)}
    });
    ops.emplace_back(JsonValue::Object{
        {"op", "object.create_text"}, {"client_ref", "pillar_2_sub"},
        {"name", "Mô tả cam kết 2"},
        {"text", "Bảo trì trọn đời tận công trình"}, {"font_family", "Segoe UI"},
        {"font_weight", 500.0}, {"font_size_pt", 16.0}, {"alignment", "left"},
        {"x_mm", 2900.0}, {"y_mm", 770.0},
        {"fill", make_rgb(1.0, 1.0, 1.0, 1.0)}
    });

    // 23. Khung cam kết 3: THI CÔNG TRỌN GÓI
    ops.emplace_back(JsonValue::Object{
        {"op", "object.create_rectangle"}, {"client_ref", "pillar_3_box"},
        {"name", "Khung cam kết 3"},
        {"x_mm", 3900.0}, {"y_mm", 600.0}, {"width_mm", 950.0}, {"height_mm", 260.0},
        {"fill", make_rgb(0.12, 0.16, 0.23, 1.0)},
        {"stroke", make_rgb(0.22, 0.74, 0.97, 1.0)}, {"stroke_width_mm", 3.0}
    });
    ops.emplace_back(JsonValue::Object{
        {"op", "object.create_text"}, {"client_ref", "pillar_3_text"},
        {"name", "Tiêu đề cam kết 3"},
        {"text", "THI CÔNG TRỌN GÓI"}, {"font_family", "Segoe UI"},
        {"font_weight", 800.0}, {"font_size_pt", 24.0}, {"alignment", "left"},
        {"x_mm", 3950.0}, {"y_mm", 690.0},
        {"fill", make_rgb(1.0, 1.0, 1.0, 1.0)}
    });
    ops.emplace_back(JsonValue::Object{
        {"op", "object.create_text"}, {"client_ref", "pillar_3_sub"},
        {"name", "Mô tả cam kết 3"},
        {"text", "Miễn phí thiết kế 3D toàn bộ"}, {"font_family", "Segoe UI"},
        {"font_weight", 500.0}, {"font_size_pt", 16.0}, {"alignment", "left"},
        {"x_mm", 3950.0}, {"y_mm", 770.0},
        {"fill", make_rgb(0.73, 0.90, 0.99, 1.0)}
    });

    // 24. Khung chân bảng thông tin liên hệ & địa chỉ khách hàng
    ops.emplace_back(JsonValue::Object{
        {"op", "object.create_rectangle"}, {"client_ref", "contact_box"},
        {"name", "Khung chân bảng thông tin liên hệ"},
        {"x_mm", 1750.0}, {"y_mm", 980.0}, {"width_mm", 3150.0}, {"height_mm", 860.0},
        {"fill", make_rgb(0.01, 0.02, 0.07, 1.0)},
        {"stroke", make_rgb(0.96, 0.62, 0.04, 1.0)}, {"stroke_width_mm", 4.0}
    });

    // 25. Dải chỉ viền vàng chân bảng
    ops.emplace_back(JsonValue::Object{
        {"op", "object.create_rectangle"}, {"client_ref", "contact_stripe"},
        {"name", "Thanh viền chỉ vàng chân bảng"},
        {"x_mm", 1750.0}, {"y_mm", 980.0}, {"width_mm", 3150.0}, {"height_mm", 14.0},
        {"fill", make_rgb(0.96, 0.62, 0.04, 1.0)},
        {"stroke", make_rgb(0.96, 0.62, 0.04, 1.0)}, {"stroke_width_mm", 0.0}
    });

    // 26. Địa chỉ khách hàng chính
    ops.emplace_back(JsonValue::Object{
        {"op", "object.create_text"}, {"client_ref", "address_text"},
        {"name", "Địa chỉ showroom khách hàng"},
        {"text", "ĐỊA CHỈ SHOWROOM: Số 168 Nguyễn Trãi, P. Thượng Đình, Q. Thanh Xuân, TP. Hà Nội"},
        {"font_family", "Segoe UI"}, {"font_weight", 700.0}, {"font_size_pt", 26.0},
        {"alignment", "left"}, {"x_mm", 1830.0}, {"y_mm", 1130.0},
        {"fill", make_rgb(1.0, 1.0, 1.0, 1.0)}
    });

    // 27. Địa chỉ nhà máy sản xuất
    ops.emplace_back(JsonValue::Object{
        {"op", "object.create_text"}, {"client_ref", "factory_text"},
        {"name", "Địa chỉ nhà máy sản xuất"},
        {"text", "NHÀ MÁY SẢN XUẤT: KCN Bình Phú, Thạch Thất, TP. Hà Nội (Quy mô 10.000m2)"},
        {"font_family", "Segoe UI"}, {"font_weight", 600.0}, {"font_size_pt", 20.0},
        {"alignment", "left"}, {"x_mm", 1830.0}, {"y_mm", 1270.0},
        {"fill", make_rgb(0.89, 0.91, 0.94, 1.0)}
    });

    // 28. Đường kẻ phân cách phần liên hệ (Line)
    ops.emplace_back(JsonValue::Object{
        {"op", "object.create_line"}, {"client_ref", "contact_divider"},
        {"name", "Đường kẻ chia phần liên hệ"},
        {"points_mm", JsonValue::Array{make_point(1830.0, 1370.0), make_point(4780.0, 1370.0)}},
        {"stroke", make_rgb(0.20, 0.25, 0.33, 1.0)}, {"stroke_width_mm", 2.0}
    });

    // 29. Hotline tư vấn
    ops.emplace_back(JsonValue::Object{
        {"op", "object.create_text"}, {"client_ref", "hotline_text"},
        {"name", "Hotline liên hệ"},
        {"text", "HOTLINE / ZALO: 0988.168.688 - 024.3888.9999 (Tư vấn miễn phí 24/7)"},
        {"font_family", "Segoe UI"}, {"font_weight", 800.0}, {"font_size_pt", 26.0},
        {"alignment", "left"}, {"x_mm", 1830.0}, {"y_mm", 1530.0},
        {"fill", make_rgb(0.98, 0.75, 0.14, 1.0)}
    });

    // 30. Website & Email
    ops.emplace_back(JsonValue::Object{
        {"op", "object.create_text"}, {"client_ref", "website_text"},
        {"name", "Website & Email khách hàng"},
        {"text", "WEBSITE: noithathoanggia.vn  -  EMAIL: cskh@noithathoanggia.vn"},
        {"font_family", "Segoe UI"}, {"font_weight", 600.0}, {"font_size_pt", 22.0},
        {"alignment", "left"}, {"x_mm", 1830.0}, {"y_mm", 1680.0},
        {"fill", make_rgb(0.22, 0.74, 0.97, 1.0)}
    });

    return ops;
}

inline void run_billboard_5x2m_design(pikdraw::desktop::NativeWindow& window,
                                      std::string_view endpoint,
                                      std::string_view access_key,
                                      std::string_view save_file_name) {
    using namespace pikdraw::automation;
    std::cout << "\n============================================================\n";
    std::cout << "[START] Thiết kế biển hiệu quảng cáo 5x2m (Vector 100%, Bố cục 1/3)\n";
    std::cout << "============================================================\n";

    const auto start_time = std::chrono::high_resolution_clock::now();
    int call_id = 100;
    const std::string pid = std::to_string(GetCurrentProcessId());

    // 1. Kết nối phiên tác vụ biên tập qua MCP
    JsonValue::Array edit_scopes{
        JsonValue("document.read"), JsonValue("document.create"),
        JsonValue("document.edit"), JsonValue("file.read"), JsonValue("file.write")
    };
    const auto session_res = call_tool(window, endpoint, access_key, call_id++, "pikdraw.connect",
        {{"instance_id", pid}, {"scopes", edit_scopes}});
    require_ok(session_res, "pikdraw.connect authoring session");
    const auto session_handle = string_value(session_res, "session_handle");

    // 2. Tạo tài liệu biển hiệu kích thước 5000 mm x 2000 mm (5m x 2m)
    const auto create_res = call_tool(window, endpoint, access_key, call_id++, "pikdraw.document.create",
        {{"session_handle", session_handle},
         {"title", "Biển hiệu quảng cáo 5x2m - Nội thất Hoàng Gia"},
         {"page_width_mm", 5000.0},
         {"page_height_mm", 2000.0}});
    require_ok(create_res, "pikdraw.document.create 5000x2000mm");
    const auto doc_id = string_value(create_res, "document_id");
    std::cout << " -> Đã tạo tài liệu 5000mm x 2000mm (5x2m), document_id=" << doc_id << "\n";

    // 3. Kết nối vào tab tài liệu mới tạo
    const auto tab_session = call_tool(window, endpoint, access_key, call_id++, "pikdraw.connect",
        {{"instance_id", pid}, {"scopes", edit_scopes}});
    require_ok(tab_session, "pikdraw.connect tab session");
    const auto design_handle = string_value(tab_session, "session_handle");

    const auto inspect_res = call_tool(window, endpoint, access_key, call_id++, "pikdraw.document.inspect",
        {{"session_handle", design_handle}});
    require_ok(inspect_res, "pikdraw.document.inspect");
    const auto epoch = string_value(inspect_res, "epoch");
    const auto revision = string_value(inspect_res, "revision");

    // 4. Chuẩn bị 30 đối tượng vector bố cục 1/3 (Logo, Khung, Cam kết, Tên, Địa chỉ, Hotline)
    const auto operations = build_billboard_5x2m_operations();
    const size_t op_count = operations.size();
    std::cout << " -> Đang thẩm định batch " << op_count << " đối tượng vector...\n";

    // 5. Thẩm định batch thao tác (Batch Validate)
    const auto val_res = call_tool(window, endpoint, access_key, call_id++, "pikdraw.batch.validate",
        {{"session_handle", design_handle},
         {"expected_epoch", epoch},
         {"expected_revision", revision},
         {"operations", operations}});
    require_ok(val_res, "pikdraw.batch.validate");
    if (!val_res.get("valid") || !*val_res.get("valid")->boolean()) {
        std::cerr << "Validation diagnostics: " << val_res.serialize() << "\n";
    }
    require(val_res.get("valid") && *val_res.get("valid")->boolean(),
        "30 vector operations batch validation failed");

    // 6. Ghi áp dụng batch vào tài liệu (Batch Apply)
    const std::string req_id = "req-billboard-" + std::to_string(GetTickCount64());
    const std::string idem_key = req_id + "-receipt";
    const auto apply_res = call_tool(window, endpoint, access_key, call_id++, "pikdraw.batch.apply",
        {{"session_handle", design_handle},
         {"request_id", req_id},
         {"idempotency_key", idem_key},
         {"expected_epoch", epoch},
         {"expected_revision", revision},
         {"operations", operations}});
    require_ok(apply_res, "pikdraw.batch.apply");
    const auto* created_ids = apply_res.get("created_ids");
    require(created_ids && created_ids->array() && created_ids->array()->size() == op_count,
        "All 30 objects must be committed with unique IDs");
    std::cout << " -> Đã ghi nhận toàn bộ " << op_count << " đối tượng vector vào Document Model.\n";

    // 7. Thẩm định hình học và typography tài liệu (Document Validate)
    const auto doc_val = call_tool(window, endpoint, access_key, call_id++, "pikdraw.document.validate",
        {{"session_handle", design_handle}, {"safe_margin_mm", 0.0}});
    require_ok(doc_val, "pikdraw.document.validate");
    require(doc_val.get("valid") && *doc_val.get("valid")->boolean(),
        "Document geometry validation failed");
    std::cout << " -> pikdraw.document.validate: Đạt chuẩn 100% hình học & typography.\n";

    // 8. Lưu tài liệu native .pikdraw
    const auto save_path = granted_file_path(save_file_name);
    require(!std::filesystem::exists(save_path),
        "unique E2E output path must not overwrite an existing native document");
    const auto save_res = call_tool(window, endpoint, access_key, call_id++, "pikdraw.file.save",
        {{"session_handle", design_handle}, {"file_name", std::string(save_file_name)}});
    require_ok(save_res, "pikdraw.file.save");
    std::cout << " -> pikdraw.file.save: Đã lưu file '" << save_file_name << ".pikdraw'.\n";

    // 9. Xác minh mở lại file đã lưu
    const auto reopen_res = call_tool(window, endpoint, access_key, call_id++, "pikdraw.file.verify_reopen",
        {{"session_handle", design_handle}, {"file_name", std::string(save_file_name)}});
    require_ok(reopen_res, "pikdraw.file.verify_reopen");
    require(reopen_res.get("reopened") && *reopen_res.get("reopened")->boolean() &&
            reopen_res.get("object_count") && *reopen_res.get("object_count")->number() == static_cast<double>(op_count),
        "Reopened native document must match object count");
    std::cout << " -> pikdraw.file.verify_reopen: Xác minh toàn vẹn file mở lại thành công.\n";

    // 10. Document evidence is semantic: geometry validation, dimensions, objects and reopen checks.

    // 11. Đóng kết nối
    call_tool(window, endpoint, access_key, call_id++, "pikdraw.disconnect", {{"session_handle", session_handle}});
    call_tool(window, endpoint, access_key, call_id++, "pikdraw.disconnect", {{"session_handle", design_handle}});

    const auto end_time = std::chrono::high_resolution_clock::now();
    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();

    std::cout << "============================================================\n";
    std::cout << "[HOÀN THÀNH] Thiết kế biển hiệu quảng cáo 5x2m thành công!\n";
    std::cout << " - Thời gian hoàn thành: " << elapsed_ms << " ms ("
              << std::fixed << std::setprecision(2) << (elapsed_ms / 1000.0) << " giây)\n";
    std::cout << " - Số lượng đối tượng vector: " << op_count << "\n";
    std::cout << " - File lưu: " << save_file_name << ".pikdraw\n";
    std::cout << "============================================================\n\n";
}
