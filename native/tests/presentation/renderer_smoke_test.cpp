#include "test_support.h"

#include "presentation/renderer.h"

#include <chrono>
#include <iostream>
#include <string>

namespace {

LRESULT CALLBACK hidden_window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    return DefWindowProcW(window, message, wparam, lparam);
}

class HiddenWindow {
public:
    HiddenWindow() {
        instance_ = GetModuleHandleW(nullptr);
        name_ = L"DesktopTodoList.RendererSmoke." + std::to_wstring(GetCurrentProcessId());
        WNDCLASSW window_class{};
        window_class.lpfnWndProc = hidden_window_proc;
        window_class.hInstance = instance_;
        window_class.lpszClassName = name_.c_str();
        registered_ = RegisterClassW(&window_class) != 0;
        if (registered_) {
            window_ = CreateWindowExW(0, name_.c_str(), L"", WS_POPUP,
                0, 0, 360, 480, nullptr, nullptr, instance_, nullptr);
        }
    }

    ~HiddenWindow() {
        if (window_ != nullptr) DestroyWindow(window_);
        if (registered_) UnregisterClassW(name_.c_str(), instance_);
    }

    [[nodiscard]] HWND handle() const noexcept { return window_; }

private:
    HINSTANCE instance_ = nullptr;
    std::wstring name_;
    HWND window_ = nullptr;
    bool registered_ = false;
};

}  // namespace

TEST_CASE(renderer_smoke_draws_visible_rows_from_a_1000_task_snapshot_within_budget) {
    HiddenWindow window;
    EXPECT_TRUE(window.handle() != nullptr);

    desktop_todo::Renderer renderer{window.handle()};
    EXPECT_TRUE(renderer.create_device_resources());

    desktop_todo::ViewModel model;
    model.rows.reserve(1'000);
    for (int index = 0; index < 1'000; ++index) {
        model.rows.push_back({
            .id = L"task-" + std::to_wstring(index),
            .title = L"Task " + std::to_wstring(index),
            .note = {},
            .priority = desktop_todo::Priority::medium,
            .status = desktop_todo::TaskStatus::todo});
    }
    model.counts = {12, 46, 1'000, 0, 4};
    const auto layout = desktop_todo::calculate_layout(
        {360.0F, 480.0F}, 96.0F, desktop_todo::LayoutMode::compact);
    const auto palette = desktop_todo::resolve_theme(
        desktop_todo::Theme::system, {});

    const auto start = std::chrono::steady_clock::now();
    renderer.draw(layout, palette, model, desktop_todo::ViewKind::today, {}, 0.0F);
    const auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_TRUE(elapsed < std::chrono::milliseconds{200});
    std::cout << "[BENCH] 1000-task visible Direct2D render: "
        << std::chrono::duration<double, std::milli>(elapsed).count() << " ms\n";
    EXPECT_TRUE(renderer.create_device_resources());
}
