#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <cstddef>

namespace pikdraw::interop {

struct XmlTag {
    std::string name;
    std::unordered_map<std::string, std::string> attributes;
    bool closing{false};
    bool self_closing{false};
};

[[nodiscard]] bool IsValidUtf8(std::string_view input);

class XmlCursor final {
public:
    explicit XmlCursor(std::string_view source) : source_(source) {}

    [[nodiscard]] bool SkipDocumentPreamble();
    [[nodiscard]] bool SkipMisc();
    [[nodiscard]] bool ParseTag(XmlTag& tag);
    [[nodiscard]] bool ReadText(std::string& text);
    [[nodiscard]] bool AtEnd() const noexcept;
    [[nodiscard]] bool AtTagStart() const noexcept;
    [[nodiscard]] std::string_view Remaining() const noexcept;
    void SkipSpace();

private:
    std::string_view source_;
    size_t position_{0};
};

} // namespace pikdraw::interop
