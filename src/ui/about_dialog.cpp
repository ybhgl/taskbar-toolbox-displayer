#include "about_dialog.h"

#include "app_info.h"
#include "logging/runtime_logger.h"
#include "ui/app_icon.h"
#include "ui/dialog_notify.h"
#include "ui/fluent_dialog_surface.h"
#include "ui/fluent_theme.h"
#include "ui/font_sizes.h"
#include "ui/release_notes_view.h"
#include "update/startup_update_check.h"
#include "update/update_service.h"

#include <windowsx.h>
#include <shellapi.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr int kIdProjectButton = 304;
constexpr int kIdCheckButton = 307;
constexpr int kIdReleaseButton = 308;
constexpr int kIdCloseButton = 309;
constexpr int kIdReleaseNotes = 312;
constexpr int kIdAutoCheckSwitch = 314;
constexpr int kIdReleaseNotesTab = 318;
constexpr int kIdDisclaimerTab = 319;

constexpr UINT kMsgUpdateReady = WM_APP + 30;
constexpr UINT kMsgDownloadProgress = WM_APP + 31;
constexpr UINT kMsgDownloadFinished = WM_APP + 32;
constexpr float kMinClientWidthDip = 440.0f;
constexpr float kMinClientHeightDip = 460.0f;
// 关于页为竖向布局，比例下限保留原设计比例，不按最小宽高直接相除。
constexpr float kMinClientAspectRatio = 520.0f / 620.0f;
constexpr DWORD kDialogStyle = WS_CAPTION | WS_SYSMENU | WS_THICKFRAME;
constexpr DWORD kDialogExStyle = WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE;

struct DownloadProgressPayload {
    uint64_t requestId = 0;
    int percent = 0;
};

struct DownloadFinishedPayload {
    uint64_t requestId = 0;
    bool success = false;
    bool cancelled = false;
    std::wstring path;
    std::wstring detail;
};

void discardPendingUpdateResults(HWND hwnd) {
    if (!hwnd)
        return;
    MSG msg{};
    while (PeekMessageW(&msg, hwnd, kMsgUpdateReady, kMsgUpdateReady, PM_REMOVE))
        delete reinterpret_cast<update_service::UpdateCheckResult*>(msg.lParam);
}

void discardPendingDownloadMessages(HWND hwnd) {
    if (!hwnd)
        return;
    MSG msg{};
    while (PeekMessageW(&msg, hwnd, kMsgDownloadProgress, kMsgDownloadProgress, PM_REMOVE))
        delete reinterpret_cast<DownloadProgressPayload*>(msg.lParam);
    while (PeekMessageW(&msg, hwnd, kMsgDownloadFinished, kMsgDownloadFinished, PM_REMOVE)) {
        auto* payload = reinterpret_cast<DownloadFinishedPayload*>(msg.lParam);
        if (payload && payload->success && !payload->path.empty())
            DeleteFileW(payload->path.c_str());
        delete payload;
    }
}

void openUrl(const wchar_t* url) {
    runtime_log::writef(L"[action][about] open-url url=%s", url ? url : L"");
    ShellExecuteW(nullptr, L"open", url, nullptr, nullptr, SW_SHOWNORMAL);
}

enum class AboutButtonIcon { None, GitHub };

// 在 16x16 坐标系中构造 GitHub Octocat 圆形镂空剪影（外圆与猫头轮廓组成单条闭合环路）。
Microsoft::WRL::ComPtr<ID2D1PathGeometry> createGitHubIconGeometry(ID2D1Factory* factory) {
    if (!factory)
        return {};
    Microsoft::WRL::ComPtr<ID2D1PathGeometry> geometry;
    if (FAILED(factory->CreatePathGeometry(&geometry)) || !geometry)
        return {};

    Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(geometry->Open(&sink)) || !sink)
        return {};

    constexpr float kCircleRadius = 7.6f;
    const D2D1_POINT_2F leftFoot{5.95f, 15.3183f};
    const D2D1_POINT_2F rightFoot{10.05f, 15.3183f};

    sink->BeginFigure(leftFoot, D2D1_FIGURE_BEGIN_FILLED);
    sink->AddArc(D2D1::ArcSegment(rightFoot, D2D1::SizeF(kCircleRadius, kCircleRadius), 0.0f,
                                  D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_LARGE));
    // 右侧躯干 -> 右脸颊 -> 右耳 -> 头顶 -> 左耳 -> 左脸颊 -> 左侧 S 形尾巴 -> 左脚
    sink->AddBezier(D2D1::BezierSegment({10.05f, 14.6f}, {10.1f, 13.0f}, {9.75f, 11.85f}));
    sink->AddBezier(D2D1::BezierSegment({11.6f, 11.6f}, {12.65f, 10.4f}, {12.65f, 8.35f}));
    sink->AddBezier(D2D1::BezierSegment({12.65f, 7.5f}, {12.55f, 6.9f}, {12.35f, 6.45f}));
    sink->AddBezier(D2D1::BezierSegment({12.65f, 5.65f}, {12.65f, 4.75f}, {12.35f, 4.15f}));
    sink->AddBezier(D2D1::BezierSegment({11.65f, 4.15f}, {10.95f, 4.65f}, {10.35f, 5.15f}));
    sink->AddBezier(D2D1::BezierSegment({9.65f, 4.9f}, {8.85f, 4.8f}, {8.0f, 4.8f}));
    sink->AddBezier(D2D1::BezierSegment({7.15f, 4.8f}, {6.35f, 4.9f}, {5.65f, 5.15f}));
    sink->AddBezier(D2D1::BezierSegment({5.05f, 4.65f}, {4.35f, 4.15f}, {3.65f, 4.15f}));
    sink->AddBezier(D2D1::BezierSegment({3.35f, 4.75f}, {3.35f, 5.65f}, {3.65f, 6.45f}));
    sink->AddBezier(D2D1::BezierSegment({3.45f, 6.9f}, {3.35f, 7.5f}, {3.35f, 8.35f}));
    sink->AddBezier(D2D1::BezierSegment({3.35f, 10.4f}, {4.4f, 11.6f}, {6.25f, 11.85f}));
    sink->AddBezier(D2D1::BezierSegment({6.05f, 12.2f}, {5.95f, 12.6f}, {5.95f, 13.05f}));
    sink->AddBezier(D2D1::BezierSegment({4.75f, 13.3f}, {3.85f, 12.65f}, {2.85f, 11.85f}));
    sink->AddBezier(D2D1::BezierSegment({3.25f, 11.75f}, {3.95f, 12.2f}, {4.35f, 12.85f}));
    sink->AddBezier(D2D1::BezierSegment({4.75f, 13.5f}, {5.25f, 13.9f}, {5.95f, 13.95f}));
    sink->AddBezier(D2D1::BezierSegment({5.95f, 14.4f}, {5.95f, 14.9f}, leftFoot));
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);

    if (FAILED(sink->Close()))
        return {};
    return geometry;
}

} // namespace

struct AboutDialog::Impl {
    HINSTANCE inst = nullptr;
    HWND hwnd = nullptr;
    HWND notifyHwnd = nullptr; // 关闭时向托盘窗口投递 kMsgDialogClosed
    bool backdrop = false;
    bool checking = false;
    bool downloading = false;
    bool installingUpdate = false;
    bool autoCheckOnStartup = true;
    bool downloadAfterCheck = false;
    uint64_t activeRequest = 0;
    std::wstring releaseUrl = app_info::kLatestReleasePage;
    std::function<void(bool)> onAutoCheckChanged;
    std::function<bool(const std::wstring&)> onInstallUpdate;
    std::optional<update_service::ReleaseInfo> latestRelease;
    std::shared_ptr<std::atomic_bool> downloadCancel;
    std::wstring downloadedInstallerPath;

    fluent::FluentDialogSurface surface;
    ReleaseNotesView releaseNotes;
    std::wstring versionText;
    std::wstring statusText;
    std::wstring latestText;
    bool releaseButtonAccent = false;
    bool releaseButtonEnabled = false;
    std::wstring releaseButtonText = L"打开发布页";

    D2D1_RECT_F autoSwitchRect{};
    D2D1_RECT_F titleRect{};
    D2D1_RECT_F subtitleRect{};
    D2D1_RECT_F infoCardRect{};
    D2D1_RECT_F versionRect{};
    D2D1_RECT_F autoCheckLabelRect{};
    D2D1_RECT_F releaseNotesTabRect{};
    D2D1_RECT_F disclaimerTabRect{};
    D2D1_RECT_F statusRect{};
    D2D1_RECT_F latestRect{};
    D2D1_RECT_F projectRect{};
    D2D1_RECT_F checkRect{};
    D2D1_RECT_F releaseRect{};
    D2D1_RECT_F closeRect{};
    D2D1_RECT_F notesRect{};
    float scrollbarRight = 0.0f;

    int hoverId = 0;
    int pressedId = 0;
    int focusedId = kIdAutoCheckSwitch;
    bool focusVisible = false;
    Microsoft::WRL::ComPtr<ID2D1PathGeometry> githubIconGeometry;

    static LRESULT CALLBACK wndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
        Impl* self = nullptr;
        if (msg == WM_NCCREATE) {
            self = static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
            self->hwnd = h;
            SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        } else {
            self = reinterpret_cast<Impl*>(GetWindowLongPtrW(h, GWLP_USERDATA));
        }
        if (self)
            return self->handle(msg, wp, lp);
        return DefWindowProcW(h, msg, wp, lp);
    }

    static bool contains(const D2D1_RECT_F& rect, float x, float y) {
        return x >= rect.left && x <= rect.right && y >= rect.top && y <= rect.bottom;
    }

    bool isEnabled(int id) const {
        if (id == kIdCheckButton)
            return !checking && !downloading && !installingUpdate && downloadedInstallerPath.empty();
        if (id == kIdReleaseButton)
            return releaseButtonEnabled && !checking && !downloading && !installingUpdate;
        return true;
    }

    void drawButtonIcon(fluent::FluentDialogSurface::Painter& painter, const D2D1_RECT_F& iconRect,
                        AboutButtonIcon icon, D2D1_COLOR_F textColor) {
        auto* rt = painter.target();
        if (!rt || icon == AboutButtonIcon::None)
            return;

        Microsoft::WRL::ComPtr<ID2D1Factory> factory;
        rt->GetFactory(&factory);
        if (!factory)
            return;

        if (icon == AboutButtonIcon::GitHub && !githubIconGeometry)
            githubIconGeometry = createGitHubIconGeometry(factory.Get());

        D2D1_MATRIX_3X2_F savedTransform{};
        rt->GetTransform(&savedTransform);
        const float scale = (iconRect.right - iconRect.left) / 16.0f;
        rt->SetTransform(D2D1::Matrix3x2F::Scale(scale, scale) *
                         D2D1::Matrix3x2F::Translation(iconRect.left, iconRect.top) *
                         savedTransform);

        if (icon == AboutButtonIcon::GitHub && githubIconGeometry) {
            if (auto* brush = painter.brush(textColor))
                rt->FillGeometry(githubIconGeometry.Get(), brush);
        }

        rt->SetTransform(savedTransform);
    }

    void drawButton(fluent::FluentDialogSurface::Painter& painter, const D2D1_RECT_F& rect,
                    const std::wstring& text, bool accent, int id,
                    AboutButtonIcon icon = AboutButtonIcon::None) {
        const auto& p = fluent::palette();
        const bool enabled = isEnabled(id);
        const bool hovered = enabled && hoverId == id;
        const bool pressed = enabled && pressedId == id;
        D2D1_COLOR_F fill{};
        D2D1_COLOR_F textColor{};
        if (!enabled) {
            fill = p.listHover;
            textColor = p.disabled;
        } else if (accent) {
            fill = pressed ? p.accentPressed : hovered ? p.accentHover : p.accent;
            textColor = p.textOnAccent;
        } else {
            fill = pressed ? p.controlPressed : hovered ? p.controlHover : p.controlFill;
            textColor = p.text;
        }
        painter.fillRoundRect(fill, rect);
        if (!accent || !enabled)
            painter.strokeRoundRect(p.controlStroke, rect);
        if (focusedId == id && focusVisible && enabled) {
            painter.strokeRoundRect(accent ? p.textOnAccent : p.accent,
                                    D2D1::RectF(rect.left + 1.5f, rect.top + 1.5f,
                                                rect.right - 1.5f, rect.bottom - 1.5f),
                                    1.5f, std::max(1.0f, fluent::metrics::controlRadius - 1.0f));
        }
        if (icon != AboutButtonIcon::None) {
            auto* leftFormat = painter.textFormat(fluent::font_sizes::button, 400, false, true);
            constexpr float kIconSize = 14.0f;
            constexpr float kIconGap = 5.0f;
            const float textW = leftFormat ? painter.measureTextWidth(text, leftFormat) : 0.0f;
            const float groupW = kIconSize + kIconGap + textW;
            const float centerX = (rect.left + rect.right) * 0.5f;
            const float centerY = (rect.top + rect.bottom) * 0.5f;
            const float groupLeft = std::max(rect.left + 6.0f, centerX - groupW * 0.5f);
            const D2D1_RECT_F iconRect =
                D2D1::RectF(groupLeft, centerY - kIconSize * 0.5f,
                            groupLeft + kIconSize, centerY + kIconSize * 0.5f);
            drawButtonIcon(painter, iconRect, icon, textColor);
            if (leftFormat) {
                painter.drawText(
                    text, leftFormat,
                    D2D1::RectF(groupLeft + kIconSize + kIconGap, rect.top,
                                rect.right - 4.0f, rect.bottom),
                    textColor);
            }
            return;
        }
        auto* format = painter.textFormat(fluent::font_sizes::button, 400, true, true);
        painter.drawText(text, format,
                         D2D1::RectF(rect.left + 4.0f, rect.top, rect.right - 4.0f, rect.bottom),
                         textColor);
    }

    void drawToggle(fluent::FluentDialogSurface::Painter& painter, const D2D1_RECT_F& rect,
                    bool checked) {
        const auto& p = fluent::palette();
        const bool enabled = true;
        const bool hovered = hoverId == kIdAutoCheckSwitch;
        const bool focused = focusedId == kIdAutoCheckSwitch && focusVisible;
        const float trackH = std::min(20.0f, rect.bottom - rect.top);
        const float centerY = (rect.top + rect.bottom) * 0.5f;
        const D2D1_RECT_F track = D2D1::RectF(
            rect.left + 0.5f, centerY - trackH * 0.5f, rect.right - 0.5f,
            centerY + trackH * 0.5f);
        const float radius = trackH * 0.5f;
        const float knobR = trackH * 0.5f - 3.5f;
        const float knobX = checked ? track.right - trackH * 0.5f : track.left + trackH * 0.5f;
        if (checked) {
            painter.fillRoundRect(hovered ? p.accentHover : p.accent, track, radius);
            if (auto* br = painter.brush(p.textOnAccent))
                painter.target()->FillEllipse(D2D1::Ellipse(D2D1::Point2F(knobX, centerY), knobR,
                                                            knobR),
                                             br);
        } else {
            painter.fillRoundRect(hovered ? p.controlHover : p.controlFill, track, radius);
            painter.strokeRoundRect(p.textSecondary, track, 1.0f, radius);
            if (auto* br = painter.brush(p.textSecondary))
                painter.target()->FillEllipse(D2D1::Ellipse(D2D1::Point2F(knobX, centerY), knobR,
                                                            knobR),
                                             br);
        }
        if (focused && enabled) {
            painter.strokeRoundRect(
                p.accent,
                D2D1::RectF(track.left + 1.5f, track.top + 1.5f,
                            track.right - 1.5f, track.bottom - 1.5f),
                1.5f, std::max(1.0f, radius - 1.5f));
        }
    }

    void drawPanelTab(fluent::FluentDialogSurface::Painter& painter, const D2D1_RECT_F& rect,
                      const wchar_t* text, bool selected, int id) {
        const auto& p = fluent::palette();
        const bool hovered = hoverId == id;
        const bool pressed = pressedId == id;
        if (selected) {
            painter.fillRoundRect(pressed ? p.controlPressed : hovered ? p.controlHover : p.controlFill,
                                  rect);
            painter.strokeRoundRect(p.controlStroke, rect);
            const float indicatorW = std::min(16.0f, std::max(0.0f, rect.right - rect.left - 16.0f));
            const float centerX = (rect.left + rect.right) * 0.5f;
            painter.fillRoundRect(
                p.accent,
                D2D1::RectF(centerX - indicatorW * 0.5f, rect.bottom - 2.5f,
                            centerX + indicatorW * 0.5f, rect.bottom - 0.5f),
                1.0f);
        } else if (pressed || hovered) {
            painter.fillRoundRect(pressed ? p.controlPressed : p.listHover, rect);
        }
        if (focusedId == id && focusVisible) {
            painter.strokeRoundRect(
                p.accent,
                D2D1::RectF(rect.left + 1.5f, rect.top + 1.5f, rect.right - 1.5f, rect.bottom - 1.5f),
                1.5f, std::max(1.0f, fluent::metrics::controlRadius - 1.0f));
        }
        auto* format = painter.textFormat(fluent::font_sizes::label, selected ? 600 : 400, true, true);
        painter.drawText(text, format, rect, selected || hovered ? p.text : p.textSecondary);
    }

    void paint(fluent::FluentDialogSurface::Painter& painter, float, float) {
        const auto& p = fluent::palette();
        painter.drawText(app_info::kName, painter.textFormat(fluent::font_sizes::brandTitle, 600), titleRect,
                         p.text);
        painter.drawText(L"把歌词带到 Windows 任务栏 · Apache-2.0 开源项目",
                         painter.textFormat(fluent::font_sizes::label, 400), subtitleRect,
                         p.textSecondary);

        painter.fillRoundRect(p.cardFill, infoCardRect, fluent::metrics::cardRadius);
        painter.strokeRoundRect(p.cardStroke, infoCardRect, 1.0f, fluent::metrics::cardRadius);
        painter.drawText(versionText, painter.textFormat(fluent::font_sizes::label, 400), versionRect, p.text);
        painter.drawText(L"启动时自动检查更新", painter.textFormat(fluent::font_sizes::label, 400), autoCheckLabelRect,
                         p.textSecondary);
        drawToggle(painter, autoSwitchRect, autoCheckOnStartup);
        drawButton(painter, projectRect, L"GitHub", false, kIdProjectButton,
                   AboutButtonIcon::GitHub);

        const AboutPanelTab activeTab = releaseNotes.activeTab();
        drawPanelTab(painter, releaseNotesTabRect, L"更新日志",
                     activeTab == AboutPanelTab::ReleaseNotes, kIdReleaseNotesTab);
        drawPanelTab(painter, disclaimerTabRect, L"免责声明",
                     activeTab == AboutPanelTab::Disclaimer, kIdDisclaimerTab);
        releaseNotes.paint(painter, notesRect, scrollbarRight);
        painter.drawText(statusText, painter.textFormat(fluent::font_sizes::label, 400), statusRect, p.textSecondary);
        painter.drawText(latestText, painter.textFormat(fluent::font_sizes::label, 400), latestRect, p.textSecondary);

        drawButton(painter, checkRect, L"检查更新", true, kIdCheckButton);
        drawButton(painter, releaseRect, releaseButtonText, releaseButtonAccent, kIdReleaseButton);
        drawButton(painter, closeRect, L"关闭", false, kIdCloseButton);
    }

    int hitTest(float x, float y) const {
        if (contains(autoSwitchRect, x, y))
            return kIdAutoCheckSwitch;
        if (contains(projectRect, x, y))
            return kIdProjectButton;
        if (contains(releaseNotesTabRect, x, y))
            return kIdReleaseNotesTab;
        if (contains(disclaimerTabRect, x, y))
            return kIdDisclaimerTab;
        if (contains(checkRect, x, y))
            return kIdCheckButton;
        if (contains(releaseRect, x, y))
            return kIdReleaseButton;
        if (contains(closeRect, x, y))
            return kIdCloseButton;
        if (contains(notesRect, x, y))
            return kIdReleaseNotes;
        return 0;
    }

    std::vector<int> focusOrder() const {
        std::vector<int> order{kIdAutoCheckSwitch, kIdProjectButton,
                               kIdReleaseNotesTab, kIdDisclaimerTab, kIdCheckButton,
                               kIdReleaseButton, kIdCloseButton};
        order.erase(std::remove_if(order.begin(), order.end(),
                                   [this](int id) { return !isEnabled(id); }),
                    order.end());
        return order;
    }

    void focusStep(int direction) {
        const auto order = focusOrder();
        if (order.empty())
            return;
        auto it = std::find(order.begin(), order.end(), focusedId);
        int index = it == order.end() ? (direction > 0 ? -1 : 0)
                                      : static_cast<int>(it - order.begin());
        index = (index + direction + static_cast<int>(order.size())) % order.size();
        focusedId = order[index];
        focusVisible = true;
        surface.invalidate();
    }

    void onCommand(int id) {
        runtime_log::writef(L"[action][about] command=%d", id);
        switch (id) {
        case kIdProjectButton:
            openUrl(app_info::kProjectUrl);
            break;
        case kIdReleaseNotesTab:
            releaseNotes.setActiveTab(AboutPanelTab::ReleaseNotes);
            break;
        case kIdDisclaimerTab:
            releaseNotes.setActiveTab(AboutPanelTab::Disclaimer);
            break;
        case kIdAutoCheckSwitch:
            autoCheckOnStartup = !autoCheckOnStartup;
            runtime_log::writef(L"[action][update] auto-check-on-startup=%s",
                                autoCheckOnStartup ? L"on" : L"off");
            if (onAutoCheckChanged)
                onAutoCheckChanged(autoCheckOnStartup);
            break;
        case kIdCheckButton:
            releaseNotes.setActiveTab(AboutPanelTab::ReleaseNotes);
            runtime_log::writef(L"[action][update] check-request");
            startCheck();
            break;
        case kIdReleaseButton:
            runtime_log::writef(L"[action][update] release-action=%s",
                                downloadedInstallerPath.empty() ? L"download" : L"install");
            if (!downloadedInstallerPath.empty())
                startInstall();
            else
                startDownload();
            break;
        case kIdCloseButton:
            destroy();
            return;
        }
        if (hwnd)
            surface.invalidate();
    }

    LRESULT handle(UINT msg, WPARAM wp, LPARAM lp) {
        switch (msg) {
        case WM_TIMER:
            if (releaseNotes.tick(wp))
                return 0;
            break;
        case WM_CREATE:
            backdrop = fluent::styleDialogWindow(hwnd);
            surface.initialize(hwnd, backdrop);
            if (!releaseNotes.create(hwnd))
                return -1;
            createControls();
            layout();
            return 0;
        case WM_SIZE:
            releaseNotes.stop();
            layout();
            surface.invalidate();
            return 0;
        case WM_GETMINMAXINFO:
            fluent::setDialogMinimumTrackSize(hwnd, reinterpret_cast<MINMAXINFO*>(lp),
                                               kDialogStyle, kDialogExStyle,
                                               kMinClientWidthDip, kMinClientHeightDip);
            return 0;
        case WM_SIZING:
            fluent::enforceDialogMinimumAspectRatio(hwnd, wp, reinterpret_cast<RECT*>(lp),
                                                     kMinClientAspectRatio);
            return TRUE;
        case WM_DPICHANGED: {
            fluent::applyDialogDpiChange(hwnd, wp, reinterpret_cast<RECT*>(lp), kDialogStyle,
                                         kDialogExStyle, kMinClientWidthDip,
                                         kMinClientHeightDip);
            layout();
            surface.invalidate();
            return 0;
        }
        case WM_SETTINGCHANGE:
        case WM_THEMECHANGED:
            releaseNotes.stop();
            backdrop = fluent::restyleDialogWindow(hwnd, backdrop);
            surface.setBackdrop(backdrop);
            surface.invalidate();
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC hdc = BeginPaint(hwnd, &ps);
            surface.paint(hdc, backdrop,
                          [this](fluent::FluentDialogSurface::Painter& painter, float w, float h) {
                              paint(painter, w, h);
                          });
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_ERASEBKGND:
            surface.eraseBackground(reinterpret_cast<HDC>(wp), backdrop);
            return 1;
        case WM_MOUSEMOVE: {
            if (releaseNotes.dragging()) {
                releaseNotes.dragTo(GET_Y_LPARAM(lp) / surface.dipScale());
                surface.invalidate();
                return 0;
            }
            if (!GetCapture()) {
                TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd, 0};
                TrackMouseEvent(&tme);
            }
            const float s = surface.dipScale();
            const int id = hitTest(GET_X_LPARAM(lp) / s, GET_Y_LPARAM(lp) / s);
            if (id != hoverId) {
                hoverId = id;
                surface.invalidate();
            }
            return 0;
        }
        case WM_MOUSELEAVE:
            if (hoverId != 0) {
                hoverId = 0;
                surface.invalidate();
            }
            return 0;
        case WM_LBUTTONDOWN: {
            SetFocus(hwnd);
            focusVisible = false;
            const float s = surface.dipScale();
            if (releaseNotes.beginScroll(GET_X_LPARAM(lp) / s, GET_Y_LPARAM(lp) / s)) {
                if (releaseNotes.dragging())
                    SetCapture(hwnd);
                surface.invalidate();
                return 0;
            }
            pressedId = hitTest(GET_X_LPARAM(lp) / s, GET_Y_LPARAM(lp) / s);
            if (pressedId == kIdReleaseNotes)
                pressedId = 0;
            if (pressedId != 0) {
                focusedId = pressedId;
                SetCapture(hwnd);
            }
            surface.invalidate();
            return 0;
        }
        case WM_LBUTTONUP: {
            if (releaseNotes.dragging()) {
                releaseNotes.endDrag();
                if (GetCapture() == hwnd)
                    ReleaseCapture();
                surface.invalidate();
                return 0;
            }
            const float s = surface.dipScale();
            const int hit = hitTest(GET_X_LPARAM(lp) / s, GET_Y_LPARAM(lp) / s);
            const int pressed = pressedId;
            pressedId = 0;
            if (GetCapture() == hwnd)
                ReleaseCapture();
            if (pressed != 0 && pressed == hit && isEnabled(pressed))
                onCommand(pressed);
            surface.invalidate();
            return 0;
        }
        case WM_CAPTURECHANGED:
            releaseNotes.endDrag();
            pressedId = 0;
            surface.invalidate();
            return 0;
        case WM_MOUSEWHEEL: {
            POINT point{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ScreenToClient(hwnd, &point);
            const float s = surface.dipScale();
            if (point.y / s >= notesRect.top && point.y / s <= notesRect.bottom &&
                point.x >= 0 && point.x / s <= scrollbarRight + 8.0f) {
                releaseNotes.scrollBy(-static_cast<float>(GET_WHEEL_DELTA_WPARAM(wp)) /
                                       WHEEL_DELTA * 64.0f);
                surface.invalidate();
            }
            return 0;
        }
        case WM_GETDLGCODE:
            return DLGC_WANTALLKEYS | DLGC_WANTTAB;
        case WM_KEYDOWN:
            if (wp == VK_TAB) {
                focusStep((GetKeyState(VK_SHIFT) & 0x8000) ? -1 : 1);
                return 0;
            }
            if (wp == VK_ESCAPE) {
                destroy();
                return 0;
            }
            if (wp == VK_LEFT || wp == VK_RIGHT) {
                if (focusedId == kIdReleaseNotesTab || focusedId == kIdDisclaimerTab) {
                    const int next = (focusedId == kIdReleaseNotesTab)
                                         ? kIdDisclaimerTab
                                         : kIdReleaseNotesTab;
                    focusedId = next;
                    onCommand(next);
                    return 0;
                }
            }
            if (wp == VK_SPACE || wp == VK_RETURN) {
                if (focusedId != 0 && isEnabled(focusedId))
                    onCommand(focusedId);
                return 0;
            }
            break;
        case WM_SETFOCUS:
            surface.invalidate();
            return 0;
        case WM_KILLFOCUS:
            focusVisible = false;
            surface.invalidate();
            return 0;
        case WM_COMMAND:
            if (HIWORD(wp) == BN_CLICKED)
                onCommand(LOWORD(wp));
            return 0;
        case kMsgUpdateReady: {
            std::unique_ptr<update_service::UpdateCheckResult> result(
                reinterpret_cast<update_service::UpdateCheckResult*>(lp));
            if (!result || result->requestId != activeRequest)
                return 0;
            runtime_log::writef(L"[action][update] check-result state=%s version=%s",
                                update_service::updateStateName(result->state),
                                result->version.c_str());
            const bool shouldDownloadAfterCheck = downloadAfterCheck;
            downloadAfterCheck = false;
            checking = false;
            latestRelease.reset();
            releaseButtonEnabled = false;
            releaseButtonAccent = false;
            releaseButtonText = L"无可用更新";
            if (result->release) {
                latestRelease = *result->release;
                releaseNotes.setRelease(std::move(*result->release));
            } else {
                releaseNotes.setMessage(result->detail.empty() ? L"暂无更新日志" : result->detail);
            }
            const std::wstring visibleVersion = update_service::displayVersion(result->version);
            if (result->state == update_service::UpdateState::Available) {
                releaseUrl = result->releaseUrl.empty()
                                 ? app_info::kLatestReleasePage
                                 : result->releaseUrl;
                if (latestRelease && !latestRelease->installerUrl.empty()) {
                    statusText = L"发现新版本：" + visibleVersion + L"，点击“下载更新”";
                    releaseButtonText = L"下载更新";
                    releaseButtonEnabled = true;
                    releaseButtonAccent = true;
                } else {
                    statusText = L"发现新版本，但该 Release 没有对应安装器";
                }
                latestText = L"最新版本：" + visibleVersion;
            } else if (result->state == update_service::UpdateState::Current) {
                releaseUrl = result->releaseUrl.empty()
                                 ? app_info::kLatestReleasePage
                                 : result->releaseUrl;
                statusText = L"当前已是最新版本";
                latestText = L"当前正式版本：" + visibleVersion;
            } else {
                releaseUrl = app_info::kLatestReleasePage;
                statusText = result->detail;
                latestText.clear();
            }
            surface.invalidate();
            if (shouldDownloadAfterCheck &&
                result->state == update_service::UpdateState::Available &&
                latestRelease && !latestRelease->installerUrl.empty())
                startDownload();
            return 0;
        }
        case kMsgDownloadProgress: {
            std::unique_ptr<DownloadProgressPayload> result(
                reinterpret_cast<DownloadProgressPayload*>(lp));
            if (!result || result->requestId != activeRequest || !downloading)
                return 0;
            statusText = L"正在下载更新安装器：" + std::to_wstring(result->percent) + L"%";
            surface.invalidate();
            return 0;
        }
        case kMsgDownloadFinished: {
            std::unique_ptr<DownloadFinishedPayload> result(
                reinterpret_cast<DownloadFinishedPayload*>(lp));
            if (!result || result->requestId != activeRequest)
                return 0;
            downloading = false;
            downloadCancel.reset();
            if (result->success) {
                runtime_log::writef(L"[action][update] download-result=success path=%s",
                                    result->path.c_str());
                downloadedInstallerPath = std::move(result->path);
                releaseButtonText = L"安装更新";
                releaseButtonEnabled = true;
                releaseButtonAccent = true;
                statusText = L"安装器下载完成，点击“安装更新”";
            } else if (!result->cancelled) {
                runtime_log::writef(L"[action][update] download-result=failed detail=%s",
                                    result->detail.c_str());
                releaseButtonText = L"重试下载";
                releaseButtonEnabled = latestRelease && !latestRelease->installerUrl.empty();
                releaseButtonAccent = releaseButtonEnabled;
                statusText = result->detail.empty() ? L"更新安装器下载失败" : result->detail;
            } else {
                runtime_log::writef(L"[action][update] download-result=cancelled");
            }
            surface.invalidate();
            return 0;
        }
        case WM_CLOSE:
            destroy();
            return 0;
        case WM_DESTROY:
            releaseNotes.reset();
            discardPendingUpdateResults(hwnd);
            discardPendingDownloadMessages(hwnd);
            githubIconGeometry.Reset();
            surface.discard();
            hwnd = nullptr;
            if (notifyHwnd)
                PostMessageW(notifyHwnd, kMsgDialogClosed,
                             static_cast<WPARAM>(DialogKind::About), 0);
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    void createControls() {
        versionText = std::wstring(L"当前版本：") + update_service::displayVersion(app_info::kVersion);
        statusText = L"点击“检查更新”获取最新版本";
        latestText.clear();
        releaseNotes.setActiveTab(AboutPanelTab::ReleaseNotes);
        releaseNotes.setMessage(L"点击“检查更新”加载更新日志");
    }

    void layout() {
        RECT rc{};
        GetClientRect(hwnd, &rc);
        const float s = surface.dipScale();
        const float clientWidth = std::max(0.0f, static_cast<float>(rc.right - rc.left) / s);
        const auto content = fluent::centeredDialogLayout(clientWidth);
        const float w = content.width;
        scrollbarRight = clientWidth - 8.0f;
        const float h = std::max(0.0f, static_cast<float>(rc.bottom - rc.top) / s);
        const float pad = fluent::metrics::pagePadding;
        const float gap = fluent::metrics::controlGap;

        const float titleH = fluent::font_sizes::brandTitle + 9.0f;
        const float subtitleH = fluent::metrics::subtitleHeight;
        titleRect = D2D1::RectF(pad, pad, std::max(pad, w - pad), pad + titleH);
        subtitleRect = D2D1::RectF(pad, pad + titleH, std::max(pad, w - pad),
                                    pad + titleH + subtitleH);

        const float cardY = pad + titleH + subtitleH + fluent::metrics::sectionGap;
        const float cardH = 100.0f;
        infoCardRect = D2D1::RectF(pad, cardY, std::max(pad, w - pad), cardY + cardH);
        const float contentX = pad + 16.0f;
        const float contentW = std::max(20.0f, w - contentX - pad - 16.0f);
        versionRect = D2D1::RectF(contentX, cardY + 14.0f, contentX + contentW,
                                   cardY + 36.0f);
        const float projectW = 84.0f;
        projectRect = D2D1::RectF(contentX, cardY + 54.0f, contentX + projectW,
                                  cardY + 86.0f);

        const float switchW = 40.0f;
        const float switchH = 20.0f;
        const float switchX = w - pad - 16.0f - switchW;
        const float autoCheckLabelLeft = projectRect.right + 12.0f;
        autoCheckLabelRect = D2D1::RectF(
            autoCheckLabelLeft, cardY + 60.0f,
            std::max(autoCheckLabelLeft + 20.0f, switchX - 8.0f), cardY + 80.0f);
        autoSwitchRect = D2D1::RectF(switchX, cardY + 60.0f + (32.0f - switchH) / 2.0f,
                                     switchX + switchW,
                                     cardY + 60.0f + (32.0f - switchH) / 2.0f + switchH);

        const float btnH = fluent::metrics::controlHeight;
        const float closeW = 88.0f;
        const float releaseW = 116.0f;
        const float checkW = 116.0f;
        const float btnY = h - pad - btnH;
        closeRect = D2D1::RectF(w - pad - closeW, btnY, w - pad, btnY + btnH);
        releaseRect = D2D1::RectF(w - pad - closeW - gap - releaseW, btnY,
                                  w - pad - closeW - gap, btnY + btnH);
        checkRect = D2D1::RectF(w - pad - closeW - gap - releaseW - gap - checkW, btnY,
                                w - pad - closeW - gap - releaseW - gap, btnY + btnH);

        const float releaseTitleY = cardY + cardH + fluent::metrics::sectionGap;
        const float tabH = 26.0f;
        const float notesY = releaseTitleY + tabH + fluent::metrics::compactGap;
        const float statusH = 22.0f;
        const float latestH = 20.0f;
        const float latestY = btnY - gap - latestH;
        const float statusY = latestY - statusH;
        const float notesBottom = statusY - gap;
        const float tabW = 74.0f;
        const float tabGap = 6.0f;
        releaseNotesTabRect = D2D1::RectF(pad, releaseTitleY,
                                          std::min(pad + tabW, w - pad),
                                          releaseTitleY + tabH);
        const float disclaimerX = releaseNotesTabRect.right + tabGap;
        disclaimerTabRect = D2D1::RectF(disclaimerX, releaseTitleY,
                                        std::min(disclaimerX + tabW, w - pad),
                                        releaseTitleY + tabH);
        notesRect = D2D1::RectF(pad, notesY, std::max(pad, w - pad),
                                std::max(notesY + 80.0f, notesBottom));
        statusRect = D2D1::RectF(pad, statusY, std::max(pad, w - pad), statusY + statusH);
        latestRect = D2D1::RectF(pad, latestY, std::max(pad, w - pad), latestY + latestH);
        fluent::offsetDialogRects(content.offsetX, {
            &autoSwitchRect, &titleRect, &subtitleRect, &infoCardRect,
            &versionRect, &autoCheckLabelRect, &releaseNotesTabRect, &disclaimerTabRect,
            &statusRect, &latestRect, &projectRect, &checkRect, &releaseRect,
            &closeRect, &notesRect});

    }

    void clearDownloadedInstaller() {
        if (!downloadedInstallerPath.empty()) {
            DeleteFileW(downloadedInstallerPath.c_str());
            downloadedInstallerPath.clear();
        }
    }

    void startDownload() {
        if (!hwnd || checking || downloading || installingUpdate || !latestRelease ||
            latestRelease->installerUrl.empty())
            return;

        clearDownloadedInstaller();
        const std::wstring outputPath =
            update_service::makeTempInstallerPath(latestRelease->installerName);
        if (outputPath.empty()) {
            runtime_log::writef(L"[action][update] download-start result=failed reason=temp-path");
            statusText = L"无法创建下载临时文件";
            surface.invalidate();
            return;
        }

        downloading = true;
        releaseButtonEnabled = false;
        releaseButtonAccent = false;
        releaseButtonText = L"下载中…";
        statusText = L"正在准备下载更新安装器…";
        activeRequest = update_service::nextRequestId();
        downloadCancel = std::make_shared<std::atomic_bool>(false);
        runtime_log::writef(L"[action][update] download-start version=%s url=%s "
                            L"size=%llu output=%s",
                            latestRelease->version.c_str(),
                            latestRelease->installerUrl.c_str(),
                            static_cast<unsigned long long>(latestRelease->installerSize),
                            outputPath.c_str());
        HWND target = hwnd;
        const uint64_t requestId = activeRequest;
        update_service::DownloadCallbacks callbacks;
        callbacks.onProgress = [target, requestId](int percent) {
            auto* payload = new DownloadProgressPayload{requestId, percent};
            if (!PostMessageW(target, kMsgDownloadProgress, 0, reinterpret_cast<LPARAM>(payload)))
                delete payload;
        };
        callbacks.onFinished = [target, requestId](bool success, bool cancelled, std::wstring path,
                                                     std::wstring detail) {
            auto* payload = new DownloadFinishedPayload{requestId, success, cancelled,
                                                        std::move(path), std::move(detail)};
            if (!PostMessageW(target, kMsgDownloadFinished, 0, reinterpret_cast<LPARAM>(payload))) {
                if (payload->success && !payload->path.empty())
                    DeleteFileW(payload->path.c_str());
                delete payload;
            }
        };
        update_service::downloadInstaller(
            latestRelease->installerUrl, outputPath, latestRelease->installerSize,
            L"GitHub", downloadCancel, std::move(callbacks));
        surface.invalidate();
    }

    void startInstall() {
        if (!hwnd || installingUpdate || downloadedInstallerPath.empty() || !onInstallUpdate)
            return;

        const std::wstring installerPath = downloadedInstallerPath;
        installingUpdate = true;
        runtime_log::writef(L"[action][update] install-start path=%s", installerPath.c_str());
        if (!onInstallUpdate(installerPath)) {
            runtime_log::writef(L"[action][update] install-start result=failed");
            installingUpdate = false;
            statusText = L"无法启动更新安装器";
            surface.invalidate();
            return;
        }

        releaseButtonEnabled = false;
        statusText = L"安装器已启动，正在退出当前程序…";
        destroy();
    }

    void startCheck() {
        if (checking || downloading || installingUpdate || !hwnd ||
            !downloadedInstallerPath.empty())
            return;
        checking = true;
        latestRelease.reset();
        releaseButtonEnabled = false;
        releaseButtonAccent = false;
        releaseButtonText = L"无可用更新";
        activeRequest = update_service::nextRequestId();
        releaseUrl = app_info::kLatestReleasePage;
        // 打开瞬间只保留一处即时反馈；旧结果（更新日志/版本标签/按钮高亮）
        // 留到 kMsgUpdateReady 统一刷新。
        statusText = L"正在检查 GitHub 更新…";
        runtime_log::writef(L"[action][update] check-start request=%llu",
                            static_cast<unsigned long long>(activeRequest));
        HWND target = hwnd;
        update_service::requestLatestRelease(
            activeRequest, [target](update_service::UpdateCheckResult result) {
                auto* payload = new update_service::UpdateCheckResult(std::move(result));
                if (!PostMessageW(target, kMsgUpdateReady, 0, reinterpret_cast<LPARAM>(payload)))
                    delete payload;
            });
    }

    void destroy() {
        if (downloadCancel)
            downloadCancel->store(true, std::memory_order_relaxed);
        if (hwnd) {
            HWND target = hwnd;
            DestroyWindow(target);
            discardPendingUpdateResults(target);
            discardPendingDownloadMessages(target);
            hwnd = nullptr;
        }
        downloadCancel.reset();
        if (!installingUpdate)
            clearDownloadedInstaller();
    }
};

AboutDialog::AboutDialog() : impl_(std::make_unique<Impl>()) {}

AboutDialog::~AboutDialog() {
    destroy();
}

bool AboutDialog::create(HINSTANCE inst, HWND parent, bool autoCheckOnStartup,
                         std::function<void(bool)> onAutoCheckChanged,
                         std::function<bool(const std::wstring&)> onInstallUpdate,
                         StartupUpdateCheck* startupCheck) {
    impl_->notifyHwnd = parent; // 仅用于关闭通知；托盘窗口不能作为普通窗口的可见所有者。
    impl_->inst = inst;
    impl_->autoCheckOnStartup = autoCheckOnStartup;
    impl_->onAutoCheckChanged = std::move(onAutoCheckChanged);
    impl_->onInstallUpdate = std::move(onInstallUpdate);
    impl_->downloadAfterCheck = false;
    impl_->checking = false;
    impl_->downloading = false;
    impl_->installingUpdate = false;
    impl_->downloadCancel.reset();
    impl_->downloadedInstallerPath.clear();
    impl_->latestRelease.reset();
    impl_->releaseButtonEnabled = false;
    impl_->releaseButtonAccent = false;
    impl_->releaseButtonText = L"无可用更新";
    impl_->activeRequest = update_service::nextRequestId();
    impl_->releaseUrl = app_info::kLatestReleasePage;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = Impl::wndProc;
    wc.hInstance = inst;
    wc.lpszClassName = L"QQMusicLyricAboutDialog";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = app_icon::windowIcon();
    RegisterClassExW(&wc);

    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    UINT dpi = GetDpiForSystem();
    float s = fluent::dipScale(dpi);
    RECT rc{0, 0, static_cast<LONG>(std::lround(600.0f * s)),
            static_cast<LONG>(std::lround(700.0f * s))};
    AdjustWindowRectExForDpi(&rc, kDialogStyle, FALSE, kDialogExStyle, dpi);
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    int x = work.left + ((work.right - work.left) - w) / 2;
    int y = work.top + ((work.bottom - work.top) - h) / 2;

    impl_->hwnd = CreateWindowExW(kDialogExStyle, L"QQMusicLyricAboutDialog", L"关于",
                                  kDialogStyle, x, y, w, h, nullptr, nullptr, inst, impl_.get());
    if (!impl_->hwnd)
        return false;
    fluent::ensureDialogMinimumSize(impl_->hwnd, kDialogStyle, kDialogExStyle,
                                     kMinClientWidthDip, kMinClientHeightDip);
    app_icon::applyWindowIcon(impl_->hwnd);

    // 弹窗触发下载时复用启动检查结果；界面仍只在用户打开时创建。
    if (startupCheck) {
        if (auto available = startupCheck->takeAvailableResult()) {
            impl_->activeRequest = available->requestId;
            auto result = std::make_unique<update_service::UpdateCheckResult>(std::move(*available));
            impl_->handle(kMsgUpdateReady, 0, reinterpret_cast<LPARAM>(result.release()));
        }
    }
    return true;
}

void AboutDialog::show(bool downloadUpdate) {
    if (impl_->hwnd) {
        const bool canStartDownload =
            downloadUpdate && !impl_->checking && !impl_->downloading &&
            !impl_->installingUpdate && impl_->downloadedInstallerPath.empty() &&
            impl_->latestRelease && !impl_->latestRelease->installerUrl.empty();
        impl_->downloadAfterCheck = downloadUpdate && !canStartDownload;
        // 普通打开关于页时重新检查；弹窗触发下载时优先复用启动检查已取得的 Release。
        if (!canStartDownload)
            impl_->startCheck();
        ShowWindow(impl_->hwnd, SW_SHOW);
        SetForegroundWindow(impl_->hwnd);
        if (canStartDownload)
            impl_->startDownload();
    }
}

void AboutDialog::destroy() {
    impl_->destroy();
}

bool AboutDialog::isOpen() const {
    return impl_->hwnd != nullptr && IsWindow(impl_->hwnd);
}

HWND AboutDialog::hwnd() const {
    return impl_->hwnd;
}
