#pragma once

#include "settings_icons.h"

#include <windows.h>

#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

// Win11 风格自绘弹出菜单：圆角、阴影、悬停高亮、勾选标记、一级子菜单，
// 以及行尾手柄拖拽排序。
namespace fluent {

using NativeMenuIcon = std::shared_ptr<std::remove_pointer_t<HICON>>;

struct FluentMenuItem {
    int id = 0;
    std::wstring text;
    settings_icon::Kind icon = settings_icon::Kind::None;
    // 可选的应用原生图标；存在时优先于线性矢量图标绘制。
    NativeMenuIcon nativeIcon;
    bool checked = false;
    bool keepOpen = false;
    // 勾选框放在行首、行尾追加拖动手柄；只从手柄发起拖拽，其余区域仍负责切换勾选。
    bool dragHandle = false;
    bool enabled = true;
    bool separator = false;
    std::vector<FluentMenuItem> submenu;
    std::wstring rightText;
};

class FluentMenu {
public:
    using Callback = std::function<void(int)>;
    // 拖拽落位回调：id 为被拖动行的标识，targetIndex 为落位后的行号（0 起）。
    using ReorderCallback = std::function<void(int id, int targetIndex)>;

    // 在屏幕坐标 pt 处弹出菜单；选中有效项时回调其 id，取消不回调。
    // 菜单为非阻塞式，由主消息循环驱动。带 dragHandle 的行默认可拖拽排序，
    // 落位时回调 reorder；未提供时手柄只作视觉提示。
    static void show(HWND owner, POINT screenPt, std::vector<FluentMenuItem> items, Callback cb,
                     ReorderCallback reorder = {});

    // 关闭当前打开的菜单（如果有）
    static void dismiss();
};

} // namespace fluent
