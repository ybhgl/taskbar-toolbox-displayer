#pragma once

#include "model/display_types.h"
#include "ui/font_sizes.h"
#include <windows.h>
#include <dwrite.h>

// 用户选择的歌词字体样式。加粗不再由样式位表达，改由字重（100-900）决定；
// 斜体仍由样式位表达，旧配置的 Bold/BoldItalic 在读取时迁移为 700。

// 字号建议使用任务栏的实际逻辑尺寸，与字体窗口的显示器缩放无关。
struct LyricFontSizeContext {
    float availableDip = 0.0f;
    float mainScale = 1.0f;
    float extraScale = 0.0f;
    float gapDip = 0.0f;
    float minSize = fluent::font_sizes::taskbar::minimum;
    float maxSize = fluent::font_sizes::taskbar::maximum;
    bool star = false;
};

inline constexpr bool isBoldFontStyle(LyricFontStyle style) {
    return style == LyricFontStyle::Bold || style == LyricFontStyle::BoldItalic;
}

inline constexpr bool isItalicFontStyle(LyricFontStyle style) {
    return style == LyricFontStyle::Italic || style == LyricFontStyle::BoldItalic;
}

inline constexpr DWRITE_FONT_STYLE dwriteStyleOf(LyricFontStyle style) {
    return isItalicFontStyle(style) ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL;
}

inline constexpr DWRITE_FONT_WEIGHT dwriteWeightOf(int weight) {
    return static_cast<DWRITE_FONT_WEIGHT>(clampFontWeight(weight));
}

// 滑杆旁与设置项摘要使用的中文档位名；非标准值按最近档位取整描述。
inline const wchar_t* fontWeightName(int weight) {
    if (weight < 200)
        return L"极细";
    if (weight < 350)
        return L"细体";
    if (weight < 450)
        return L"常规";
    if (weight < 550)
        return L"中等";
    if (weight < 650)
        return L"半粗";
    if (weight < 800)
        return L"加粗";
    return L"特粗";
}
