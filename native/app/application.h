#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <memory>

namespace desktop_todo {

class Application {
public:
    Application();
    ~Application();
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    int run(HINSTANCE instance, int show_command);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace desktop_todo
