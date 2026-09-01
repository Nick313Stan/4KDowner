#pragma once

#include "DockArea.h"
#include "IEmojiBackend.h"

#include "raylib.h"

#include <memory>

class Application
{
public:
    Application();
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    void Run();
    void DrawFrame();

private:
    void Update();
    void Draw();

    Font font_{};
    // Native grayscale AA font for footer FPS/version (see DockArea::kFooterMetaFontSize).
    Font fontFooterAa_{};
    std::unique_ptr<IEmojiBackend> emojiBackend_;
    DockArea dockArea_;
};
