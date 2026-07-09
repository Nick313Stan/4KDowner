#pragma once

#include "DockArea.h"

#include "raylib.h"

class Application {
public:
    Application();
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    void Run();

private:
    void Update();
    void Draw();

    Font font_{};
    DockArea dockArea_;
};
