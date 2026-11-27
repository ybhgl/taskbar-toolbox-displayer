#pragma once

namespace app_info {

inline constexpr wchar_t kName[] = L"Windows 任务栏歌词";
inline constexpr wchar_t kEnglishName[] = L"Windows Taskbar Lyrics";

// 发布新版本时同步修改这里，并使用相同的版本号创建 GitHub Release 标签。
inline constexpr wchar_t kVersion[] = L"2.25.0";
inline constexpr wchar_t kProjectUrl[] =
    L"https://github.com/ybhgl/taskbar-lyrics-displayer";
inline constexpr wchar_t kLatestReleasePage[] =
    L"https://github.com/ybhgl/taskbar-lyrics-displayer/releases/latest";
inline constexpr char kLatestReleaseApi[] =
    "https://api.github.com/repos/ybhgl/taskbar-lyrics-displayer/releases/latest";

} // namespace app_info
