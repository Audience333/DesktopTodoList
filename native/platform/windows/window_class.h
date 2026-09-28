#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <string>

namespace desktop_todo {

class WindowClass {
public:
    WindowClass(HINSTANCE instance, std::wstring name, WNDPROC procedure, HBRUSH background);
    ~WindowClass();
    WindowClass(const WindowClass&) = delete;
    WindowClass& operator=(const WindowClass&) = delete;

    [[nodiscard]] bool registered() const noexcept;
    [[nodiscard]] const wchar_t* name() const noexcept;

private:
    HINSTANCE instance_ = nullptr;
    std::wstring name_;
    ATOM atom_ = 0;
};

}  // namespace desktop_todo
