#include "Version.h"

#include "Application.h"

#include "ConverterInfoLoader.h"
#include "EmojiText.h"
#include "LinkInfoLoader.h"
#include "ShortcutRouter.h"
#include "WinAppPaths.h"
#include "rlgl.h"

#include <filesystem>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#define NOGDI
#define CloseWindow Win32CloseWindow
#define ShowCursor Win32ShowCursor
#include <windows.h>
#undef CloseWindow
#undef ShowCursor
#undef DrawTextEx
#undef LoadImage
#undef DrawText
#endif

namespace
{
std::vector<int> BuildFontCodepoints()
{
    std::vector<int> codepoints;
    for (int codepoint = 0x0020; codepoint <= 0x00FF; ++codepoint)
    {
        codepoints.push_back(codepoint);
    }
    for (int codepoint = 0x0100; codepoint <= 0x024F; ++codepoint)
    {
        codepoints.push_back(codepoint);
    }
    for (int codepoint = 0x0400; codepoint <= 0x052F; ++codepoint)
    {
        codepoints.push_back(codepoint);
    }
    for (int codepoint = 0x2000; codepoint <= 0x206F; ++codepoint)
    {
        codepoints.push_back(codepoint);
    }
    for (int codepoint = 0xFF00; codepoint <= 0xFFEF; ++codepoint)
    {
        codepoints.push_back(codepoint);
    }
    return codepoints;
}

Font LoadFontWithType(const std::filesystem::path& fontPath,
                      int fontSize,
                      const std::vector<int>& codepoints,
                      FontType type)
{
    Font font{};
    int dataSize = 0;
    unsigned char* fileData = LoadFileData(fontPath.string().c_str(), &dataSize);
    if (fileData == nullptr || dataSize <= 0)
    {
        return font;
    }

    font.baseSize = fontSize;
    font.glyphs = LoadFontData(fileData,
                               dataSize,
                               fontSize,
                               codepoints.data(),
                               static_cast<int>(codepoints.size()),
                               static_cast<int>(type),
                               &font.glyphCount);
    UnloadFileData(fileData);

    if (font.glyphs == nullptr || font.glyphCount <= 0)
    {
        font = GetFontDefault();
        return font;
    }

    constexpr int kGlyphPadding = 4;
    font.glyphPadding = kGlyphPadding;
    Image atlas = GenImageFontAtlas(font.glyphs, &font.recs, font.glyphCount, font.baseSize, font.glyphPadding, 0);
    font.texture = LoadTextureFromImage(atlas);
    for (int i = 0; i < font.glyphCount; ++i)
    {
        UnloadImage(font.glyphs[i].image);
        font.glyphs[i].image = ImageFromImage(atlas, font.recs[i]);
    }
    UnloadImage(atlas);

    SetTextureFilter(font.texture, type == FONT_BITMAP ? TEXTURE_FILTER_POINT : TEXTURE_FILTER_BILINEAR);
    return font;
}

#ifdef _WIN32
Application* g_application = nullptr;
WNDPROC g_previousWndProc = nullptr;
bool g_drawingFromResize = false;

void DrawDuringWindowResize()
{
    if (g_application == nullptr || g_drawingFromResize)
    {
        return;
    }

    // Windows runs a modal message loop while the user drags the resize border.
    // Raylib's main loop is blocked, so redraw here after GLFW updates screen size.
    g_drawingFromResize = true;
    BeginDrawing();
    g_application->DrawFrame();
    rlDrawRenderBatchActive();
    SwapScreenBuffer();
    g_drawingFromResize = false;
}

LRESULT CALLBACK ResizableWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    const LRESULT result = CallWindowProc(g_previousWndProc, hwnd, message, wParam, lParam);
    if (message == WM_SIZE && wParam != SIZE_MINIMIZED)
    {
        DrawDuringWindowResize();
    }
    return result;
}

void InstallLiveResizeHook()
{
    HWND hwnd = static_cast<HWND>(GetWindowHandle());
    if (hwnd == nullptr)
    {
        return;
    }
    g_previousWndProc =
        reinterpret_cast<WNDPROC>(SetWindowLongPtr(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(ResizableWndProc)));
}

void RemoveLiveResizeHook()
{
    HWND hwnd = static_cast<HWND>(GetWindowHandle());
    if (hwnd != nullptr && g_previousWndProc != nullptr)
    {
        SetWindowLongPtr(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(g_previousWndProc));
    }
    g_previousWndProc = nullptr;
}
#endif
} // namespace

Application::Application()
{
    SetWorkingDirectoryToExecutable();
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(800, 633, "4KDowner " FOURKDOWNER_VERSION);
    SetWindowMinSize(600, 487);
    SetExitKey(KEY_NULL);

#ifdef _WIN32
    g_application = this;
    InstallLiveResizeHook();
#endif

    const std::filesystem::path logoPath = FindAssetPath(std::filesystem::path("assets") / "logo" / "logo.png");
    Image logoImage = LoadImage(logoPath.string().c_str());
    if (logoImage.data != nullptr)
    {
        SetWindowIcon(logoImage);
        UnloadImage(logoImage);
    }

    std::vector<int> codepoints = BuildFontCodepoints();
    const std::filesystem::path fontPath =
        FindAssetPath(std::filesystem::path("assets") / "fonts" / "InterVariable.ttf");
    font_ = LoadFontEx(fontPath.string().c_str(), 48, codepoints.data(), static_cast<int>(codepoints.size()));
    SetTextureFilter(font_.texture, TEXTURE_FILTER_BILINEAR);

    // Native bake for footer FPS/version (drawn at DockArea::kFooterMetaFontSize).
    constexpr int kFooterAaFontSize = static_cast<int>(DockArea::kFooterMetaFontSize + 0.5f);
    std::vector<int> footerCodepoints;
    for (int codepoint = 0x0020; codepoint <= 0x007E; ++codepoint)
    {
        footerCodepoints.push_back(codepoint);
    }
    fontFooterAa_ = LoadFontWithType(fontPath, kFooterAaFontSize, footerCodepoints, FONT_DEFAULT);
    SetTargetFPS(60);

    // Default emoji backend: Noto-like PNG sprites (Twemoji via EmojiBackendKind::Sprites).
    emojiBackend_ = CreateEmojiBackend(EmojiBackendKind::NotoSprites);
    EmojiText::SetBackend(emojiBackend_.get());
}

Application::~Application()
{
#ifdef _WIN32
    RemoveLiveResizeHook();
    g_application = nullptr;
#endif
    dockArea_.UnloadResources();
    EmojiText::SetBackend(nullptr);
    if (emojiBackend_ != nullptr)
    {
        emojiBackend_->UnloadAll();
        emojiBackend_.reset();
    }
    UnloadFont(fontFooterAa_);
    UnloadFont(font_);
    CloseWindow();
}

void Application::Run()
{
    while (!WindowShouldClose())
    {
        if (ShortcutRouter::Pressed({KEY_Q, true, false, false}))
        {
            break;
        }

        Update();
        Draw();
    }
}

void Application::Update()
{
    EmojiText::Pump();
    LinkInfoLoader::ReapAbandoned();
    ConverterInfoLoader::ReapAbandoned();
    dockArea_.Update(GetScreenWidth(), GetScreenHeight(), font_);
}

void Application::DrawFrame()
{
    ClearBackground({5, 9, 5, 255});
    dockArea_.Draw(GetScreenWidth(), GetScreenHeight(), font_, fontFooterAa_);
}

void Application::Draw()
{
    BeginDrawing();
    DrawFrame();
    EndDrawing();
}
