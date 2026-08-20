#pragma once

#include "DockArea.h"

#include "raylib.h"

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
    // Native 12px grayscale AA font for footer FPS/version.
    Font fontFooterAa_{};
    DockArea dockArea_;
};
