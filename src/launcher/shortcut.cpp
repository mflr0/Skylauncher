#include "launcher/shortcut.h"
#include "launcher/env.h"
#include "launcher/i18n.h"
#include "shared/collection/datadir.h"
#include <SDL3/SDL.h>
#include <stb_image.h>
#include <stb_image_write.h>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>

#if defined(__APPLE__)
#include <limits.h>
#include <mach-o/dyld.h>
#elif defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#include <shobjidl.h>
#include <shlobj.h>
#else
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace
{
    bool WriteBytes(const fs::path& path, const void* data, size_t size)
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file.write(static_cast<const char*>(data), std::streamsize(size));
        return file.good();
    }

    bool WriteText(const fs::path& path, const std::string& text) { return WriteBytes(path, text.data(), text.size()); }

    std::string XmlEscape(std::string_view text)
    {
        std::string out;
        for (char c : text)
            switch (c)
            {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            default: out += c;
            }
        return out;
    }

    // The width and height of a PNG from its header; false when the bytes are not a PNG.
    bool PngSize(const std::vector<uint8_t>& png, uint32_t& width, uint32_t& height)
    {
        static const uint8_t Signature[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
        if (png.size() < 24 || !std::equal(Signature, Signature + 8, png.begin()) || !std::equal(png.begin() + 12, png.begin() + 16, "IHDR"))
            return false;
        auto be = [&](size_t at) { return uint32_t(png[at]) << 24 | uint32_t(png[at + 1]) << 16 | uint32_t(png[at + 2]) << 8 | png[at + 3]; };
        width = be(16);
        height = be(20);
        return width > 0 && height > 0;
    }

    void Put32(std::vector<uint8_t>& out, uint32_t value, bool bigEndian)
    {
        for (int i = 0; i < 4; i++)
            out.push_back(uint8_t(value >> (bigEndian ? 24 - 8 * i : 8 * i)));
    }

    void Put16(std::vector<uint8_t>& out, uint16_t value)
    {
        out.push_back(uint8_t(value));
        out.push_back(uint8_t(value >> 8));
    }

    ShortcutResult Fail(std::string error)
    {
        ShortcutResult result;
        result.error = std::move(error);
        return result;
    }

    // Where the icon file goes, and writes it: Windows and Linux shortcuts point at it, so it lives in the game's folder.
    bool WriteIcon(const fs::path& file, const std::vector<uint8_t>& bytes)
    {
        std::error_code error;
        fs::create_directories(file.parent_path(), error);
        return !bytes.empty() && WriteBytes(file, bytes.data(), bytes.size());
    }
}

std::string ShortcutFileName(std::string_view gameName)
{
    std::string name;
    for (char c : gameName)
    {
        if (c == ':')
            continue;
        name += std::string_view("\\/*?\"<>|").find(c) != std::string_view::npos || static_cast<unsigned char>(c) < 32 ? ' ' : c;
    }
    // Words separated by one space, none at the ends or a dot at the end (Windows).
    std::string out;
    for (char c : name)
        if (c != ' ' || (!out.empty() && out.back() != ' '))
            out += c;
    while (!out.empty() && (out.back() == ' ' || out.back() == '.'))
        out.pop_back();
    return out.empty() ? "Skylanders" : out;
}

std::string ShortcutArguments(std::string_view gameId) { return "--game " + std::string(gameId) + " --play"; }

std::string ShellQuote(std::string_view text)
{
    std::string out = "'";
    for (char c : text)
        out += c == '\'' ? std::string("'\\''") : std::string(1, c);
    return out + "'";
}

std::string DesktopExecQuote(std::string_view text)
{
    std::string out = "\"";
    for (char c : text)
    {
        if (c == '\\')
            out += "\\\\\\\\"; // a backslash: escaped in the quoting, then again in the file's string syntax
        else if (c == '"' || c == '`' || c == '$')
            out += std::string("\\") + c;
        else if (c == '%')
            out += "%%";
        else
            out += c;
    }
    return out + "\"";
}

std::string MacLaunchScript(const fs::path& launcher, std::string_view gameId)
{
    return "#!/bin/sh\n# Opens Arkeyan Launcher on this game and starts it. Made by Arkeyan Launcher (Game settings).\nexec " +
        ShellQuote(datadir::Text(launcher)) + " " + ShortcutArguments(gameId) + "\n";
}

std::string MacInfoPlist(std::string_view gameName, std::string_view gameId, bool icon)
{
    std::string name = XmlEscape(gameName);
    return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
           "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
           "<plist version=\"1.0\">\n<dict>\n"
           "\t<key>CFBundleName</key>\n\t<string>" + name + "</string>\n"
           "\t<key>CFBundleDisplayName</key>\n\t<string>" + name + "</string>\n"
           "\t<key>CFBundleIdentifier</key>\n\t<string>io.github.mflr0.skylauncher.shortcut." + XmlEscape(gameId) + "</string>\n"
           "\t<key>CFBundleExecutable</key>\n\t<string>launch</string>\n"
           "\t<key>CFBundlePackageType</key>\n\t<string>APPL</string>\n"
           "\t<key>CFBundleShortVersionString</key>\n\t<string>1.0</string>\n"
           "\t<key>CFBundleVersion</key>\n\t<string>1</string>\n" +
           (icon ? "\t<key>CFBundleIconFile</key>\n\t<string>icon</string>\n" : std::string()) +
           "</dict>\n</plist>\n";
}

std::vector<uint8_t> MacIcns(const std::vector<uint8_t>& png)
{
    uint32_t width = 0, height = 0;
    if (!PngSize(png, width, height) || width != height)
        return {};
    const char* type = width == 16 ? "icp4" : width == 32 ? "icp5" : width == 64 ? "icp6" : width == 128 ? "ic07" : width == 256 ? "ic08" :
        width == 512 ? "ic09" : width == 1024 ? "ic10" : nullptr;
    if (type == nullptr)
        return {};
    std::vector<uint8_t> out = { 'i', 'c', 'n', 's' };
    Put32(out, uint32_t(8 + 8 + png.size()), true);
    out.insert(out.end(), type, type + 4);
    Put32(out, uint32_t(8 + png.size()), true);
    out.insert(out.end(), png.begin(), png.end());
    return out;
}

std::string LinuxDesktopEntry(std::string_view gameName, const fs::path& launcher, std::string_view gameId, const fs::path& icon)
{
    std::string text = "[Desktop Entry]\nType=Application\nVersion=1.0\nName=" + std::string(gameName) + "\nComment=" +
        Tf("Play {} with Arkeyan Launcher", gameName) + "\nExec=" + DesktopExecQuote(datadir::Text(launcher)) + " " + ShortcutArguments(gameId) + "\n";
    if (!icon.empty())
        text += "Icon=" + datadir::Text(icon) + "\n";
    return text + "Terminal=false\nCategories=Game;\n";
}

namespace
{
    // An .ico entry is at most 256 px: a larger square PNG (the game's 512 px icon) is halved, 2 x 2 pixels averaged, until it
    // fits. Anything that does not decode is left as it is.
    std::vector<uint8_t> FitIcoPng(std::vector<uint8_t> png)
    {
        uint32_t width = 0, height = 0;
        while (PngSize(png, width, height) && width == height && width > 256 && width % 2 == 0)
        {
            int w = 0, h = 0, channels = 0;
            stbi_uc* pixels = stbi_load_from_memory(png.data(), int(png.size()), &w, &h, &channels, 4);
            if (pixels == nullptr || uint32_t(w) != width || uint32_t(h) != height)
            {
                stbi_image_free(pixels);
                break;
            }
            std::vector<uint8_t> half(size_t(w / 2) * size_t(h / 2) * 4);
            for (int y = 0; y < h / 2; y++)
                for (int x = 0; x < w / 2; x++)
                    for (int c = 0; c < 4; c++)
                    {
                        int sum = 0;
                        for (int dy = 0; dy < 2; dy++)
                            for (int dx = 0; dx < 2; dx++)
                                sum += pixels[((size_t(2 * y + dy) * w) + size_t(2 * x + dx)) * 4 + c];
                        half[(size_t(y) * (w / 2) + x) * 4 + c] = uint8_t((sum + 2) / 4);
                    }
            stbi_image_free(pixels);
            std::vector<uint8_t> out;
            auto append = [](void* context, void* data, int size) {
                auto* bytes = static_cast<std::vector<uint8_t>*>(context);
                bytes->insert(bytes->end(), static_cast<uint8_t*>(data), static_cast<uint8_t*>(data) + size);
            };
            if (!stbi_write_png_to_func(append, &out, w / 2, h / 2, 4, half.data(), (w / 2) * 4))
                break;
            png = std::move(out);
        }
        return png;
    }
}

std::vector<uint8_t> WindowsIco(const std::vector<uint8_t>& smallOrBigPng)
{
    uint32_t width = 0, height = 0;
    std::vector<uint8_t> png = FitIcoPng(smallOrBigPng);
    if (!PngSize(png, width, height))
        return {};
    std::vector<uint8_t> out;
    Put16(out, 0); // reserved
    Put16(out, 1); // an icon
    Put16(out, 1); // one image
    out.push_back(uint8_t(width >= 256 ? 0 : width)); // 0 means 256
    out.push_back(uint8_t(height >= 256 ? 0 : height));
    out.push_back(0); // no palette
    out.push_back(0);
    Put16(out, 1);  // planes
    Put16(out, 32); // bits per pixel
    Put32(out, uint32_t(png.size()), false);
    Put32(out, 22, false); // the image follows the 6 + 16 bytes of header
    out.insert(out.end(), png.begin(), png.end());
    return out;
}

fs::path DesktopFolder()
{
    if (const char* folder = LauncherEnv("SKY_LAUNCHER_DESKTOP"); folder != nullptr && *folder != 0)
        return datadir::FromText(folder);
    if (const char* folder = SDL_GetUserFolder(SDL_FOLDER_DESKTOP))
        return datadir::FromText(folder);
    return {};
}

fs::path SelfExecutable()
{
    if (const char* exe = LauncherEnv("SKY_LAUNCHER_EXE"); exe != nullptr && *exe != 0)
        return datadir::FromText(exe);
#if defined(__APPLE__)
    char buffer[PATH_MAX];
    uint32_t size = sizeof(buffer);
    std::error_code error;
    if (_NSGetExecutablePath(buffer, &size) == 0)
        return fs::weakly_canonical(buffer, error);
    return {};
#elif defined(_WIN32)
    wchar_t buffer[MAX_PATH * 4];
    DWORD length = GetModuleFileNameW(nullptr, buffer, DWORD(std::size(buffer)));
    return length > 0 && length < std::size(buffer) ? fs::path(std::wstring(buffer, length)) : fs::path();
#else
    // An AppImage runs from a temporary mount: the shortcut must start the file the player has.
    if (const char* image = SDL_getenv("APPIMAGE"); image != nullptr && *image != 0)
        return datadir::FromText(image);
    std::error_code error;
    return fs::read_symlink("/proc/self/exe", error);
#endif
}

ShortcutResult WriteMacApp(const ShortcutSpec& spec)
{
    std::error_code error;
    fs::path app = spec.folder / (ShortcutFileName(spec.gameName) + ".app");
    fs::remove_all(app, error);
    fs::create_directories(app / "Contents" / "MacOS", error);
    fs::create_directories(app / "Contents" / "Resources", error);
    if (error)
        return Fail(Tf("Couldn't create the shortcut: {}", error.message()));
    std::vector<uint8_t> icns = MacIcns(spec.iconPng);
    bool ok = WriteText(app / "Contents" / "Info.plist", MacInfoPlist(spec.gameName, spec.gameId, !icns.empty())) &&
        WriteText(app / "Contents" / "MacOS" / "launch", MacLaunchScript(spec.launcher, spec.gameId)) &&
        (icns.empty() || WriteBytes(app / "Contents" / "Resources" / "icon.icns", icns.data(), icns.size()));
    fs::permissions(app / "Contents" / "MacOS" / "launch", fs::perms::owner_all | fs::perms::group_read | fs::perms::group_exec |
        fs::perms::others_read | fs::perms::others_exec, error);
    if (!ok)
        return Fail(Tr("Couldn't create the shortcut"));
    fs::last_write_time(app, fs::file_time_type::clock::now(), error); // the Finder looks at the icon again
    ShortcutResult result;
    result.ok = true;
    result.file = app;
    return result;
}

ShortcutResult WriteLinuxDesktop(const ShortcutSpec& spec)
{
    std::string lower = ShortcutFileName(spec.gameName);
    for (char& c : lower)
        c = c == ' ' ? '-' : char(std::tolower(static_cast<unsigned char>(c)));
    fs::path icon = spec.iconFolder / "shortcut-icon.png";
    if (!WriteIcon(icon, spec.iconPng))
        icon.clear();
    std::error_code error;
    fs::create_directories(spec.folder, error);
    fs::path file = spec.folder / (lower + ".desktop");
    if (!WriteText(file, LinuxDesktopEntry(spec.gameName, spec.launcher, spec.gameId, icon)))
        return Fail(Tr("Couldn't create the shortcut"));
    // Desktops launch a .desktop file only when it is executable.
    fs::permissions(file, fs::perms::owner_all | fs::perms::group_read | fs::perms::group_exec | fs::perms::others_read | fs::perms::others_exec, error);
    ShortcutResult result;
    result.ok = true;
    result.file = file;
    return result;
}

#if defined(_WIN32)
namespace
{
    ShortcutResult WriteWindowsLnk(const ShortcutSpec& spec)
    {
        fs::path icon = spec.iconFolder / "shortcut.ico";
        if (!WriteIcon(icon, WindowsIco(spec.iconPng)))
            icon.clear();
        std::error_code error;
        fs::create_directories(spec.folder, error);
        fs::path file = spec.folder / (ShortcutFileName(spec.gameName) + ".lnk");

        HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        IShellLinkW* link = nullptr;
        bool ok = SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, reinterpret_cast<void**>(&link)));
        if (ok)
        {
            std::string arguments = ShortcutArguments(spec.gameId); // ASCII
            std::wstring wideArguments(arguments.begin(), arguments.end());
            std::wstring description = datadir::FromText(Tf("Play {}", spec.gameName)).wstring();
            link->SetPath(spec.launcher.c_str());
            link->SetArguments(wideArguments.c_str());
            link->SetWorkingDirectory(spec.launcher.parent_path().c_str());
            link->SetDescription(description.c_str());
            if (!icon.empty())
                link->SetIconLocation(icon.c_str(), 0);
            IPersistFile* persist = nullptr;
            ok = SUCCEEDED(link->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&persist)));
            if (ok)
            {
                ok = SUCCEEDED(persist->Save(file.c_str(), TRUE));
                persist->Release();
            }
            link->Release();
        }
        if (SUCCEEDED(init))
            CoUninitialize();
        if (!ok)
            return Fail(Tr("Couldn't create the shortcut"));
        ShortcutResult result;
        result.ok = true;
        result.file = file;
        return result;
    }
}
#endif

namespace
{
    std::string ReadText(const fs::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(file), {});
    }

    // The value of the first "<key>=" line of a .desktop file.
    std::string DesktopValue(const std::string& text, const std::string& key)
    {
        size_t at = text.find("\n" + key + "=");
        if (at == std::string::npos)
            return {};
        size_t begin = at + key.size() + 2;
        return text.substr(begin, text.find('\n', begin) - begin);
    }
}

int RefreshShortcuts(const fs::path& folder, const std::vector<std::pair<std::string, std::string>>& games, const fs::path& launcher)
{
    if (folder.empty() || launcher.empty())
        return 0;
    int rewritten = 0;
    std::error_code error;
    for (const auto& [id, name] : games)
    {
        const std::string arguments = ShortcutArguments(id);
        // Ours: it starts this game the way the launcher writes it. Old: it still says Skylauncher.
        auto old = [&](const std::string& text) { return text.find(arguments) != std::string::npos && text.find("Skylauncher") != std::string::npos; };
        // macOS: the script of the small app.
        fs::path script = folder / (ShortcutFileName(name) + ".app") / "Contents" / "MacOS" / "launch";
        if (fs::is_regular_file(script, error) && !fs::is_symlink(script, error))
        {
            std::string text = ReadText(script), fresh = MacLaunchScript(launcher, id);
            if (old(text) && text != fresh && WriteText(script, fresh))
                rewritten++;
        }
        // Linux: the .desktop file, its icon kept.
        std::string lower = ShortcutFileName(name);
        for (char& c : lower)
            c = c == ' ' ? '-' : char(std::tolower(static_cast<unsigned char>(c)));
        fs::path desktop = folder / (lower + ".desktop");
        if (fs::is_regular_file(desktop, error) && !fs::is_symlink(desktop, error))
        {
            std::string text = ReadText(desktop);
            std::string fresh = LinuxDesktopEntry(DesktopValue(text, "Name").empty() ? name : DesktopValue(text, "Name"), launcher, id,
                datadir::FromText(DesktopValue(text, "Icon")));
            if (old(text) && text != fresh && WriteText(desktop, fresh))
                rewritten++;
        }
    }
    return rewritten;
}

ShortcutResult CreateDesktopShortcut(const ShortcutSpec& spec)
{
    if (spec.folder.empty())
        return Fail(Tr("This computer has no Desktop folder."));
    if (spec.launcher.empty())
        return Fail(Tr("Arkeyan Launcher couldn't find its own file."));
    std::error_code error;
    fs::create_directories(spec.folder, error);
    if (!fs::is_directory(spec.folder, error))
        return Fail(Tr("Can't write to the Desktop folder."));
#if defined(__APPLE__)
    return WriteMacApp(spec);
#elif defined(_WIN32)
    return WriteWindowsLnk(spec);
#else
    return WriteLinuxDesktop(spec);
#endif
}
