#include "Application.h"

#include "WinAppPaths.h"

#include <filesystem>
#include <vector>

namespace {
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

std::filesystem::path FindAssetPath(const std::filesystem::path& relativePath)
{
    std::filesystem::path directory = std::filesystem::current_path();
    while (!directory.empty())
    {
        const std::filesystem::path candidate = directory / relativePath;
        if (std::filesystem::exists(candidate))
        {
            return std::filesystem::absolute(candidate);
        }

        const std::filesystem::path parent = directory.parent_path();
        if (parent == directory)
        {
            break;
        }
        directory = parent;
    }

    return relativePath;
}
}

Application::Application()
{
    SetWorkingDirectoryToExecutable();
    InitWindow(720, 500, "4KDowner 1.0");

    const std::filesystem::path logoPath =
        FindAssetPath(std::filesystem::path("assets") / "logo" / "logo.png");
    Image logoImage = LoadImage(logoPath.string().c_str());
    if (logoImage.data != nullptr)
    {
        SetWindowIcon(logoImage);
        UnloadImage(logoImage);
    }

    std::vector<int> codepoints = BuildFontCodepoints();
    const std::filesystem::path fontPath = FindAssetPath(std::filesystem::path("assets") / "fonts" / "InterVariable.ttf");
    font_ = LoadFontEx(fontPath.string().c_str(), 48, codepoints.data(), static_cast<int>(codepoints.size()));
    SetTextureFilter(font_.texture, TEXTURE_FILTER_BILINEAR);
    SetTargetFPS(60);
}

Application::~Application()
{
    dockArea_.UnloadResources();
    UnloadFont(font_);
    CloseWindow();
}

void Application::Run()
{
    while (!WindowShouldClose())
    {
        Update();
        Draw();
    }
}

void Application::Update()
{
    dockArea_.Update(GetScreenWidth(), GetScreenHeight(), font_);
}

void Application::Draw()
{
    BeginDrawing();
    ClearBackground({5, 9, 5, 255});
    dockArea_.Draw(GetScreenWidth(), GetScreenHeight(), font_);
    EndDrawing();
}
