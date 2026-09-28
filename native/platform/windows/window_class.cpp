#include "platform/windows/window_class.h"

namespace desktop_todo {

WindowClass::WindowClass(
    HINSTANCE instance, std::wstring name, WNDPROC procedure, HBRUSH background)
    : instance_(instance), name_(std::move(name)) {
    WNDCLASSEXW value{};
    value.cbSize = sizeof(value);
    value.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    value.lpfnWndProc = procedure;
    value.hInstance = instance_;
    value.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    value.hbrBackground = background;
    value.lpszClassName = name_.c_str();
    atom_ = RegisterClassExW(&value);
}

WindowClass::~WindowClass() {
    if (atom_ != 0) UnregisterClassW(name_.c_str(), instance_);
}

bool WindowClass::registered() const noexcept { return atom_ != 0; }
const wchar_t* WindowClass::name() const noexcept { return name_.c_str(); }

}  // namespace desktop_todo
