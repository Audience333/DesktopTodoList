#include "platform/windows/autostart_service.h"

#include "tests/test_support.h"

#include <string>

using namespace desktop_todo;

TEST_CASE(autostart_service_writes_exact_hkcu_run_value_name_and_quoted_path) {
    std::wstring value_name;
    std::wstring command;
    AutostartService service{{
        [&value_name, &command](std::wstring_view name, std::wstring_view value) {
            value_name = name;
            command = value;
            return ERROR_SUCCESS;
        },
        [](std::wstring_view) { return ERROR_SUCCESS; }}};

    const auto result = service.set_enabled(true, L"C:/Program Files/DesktopTodoList.exe");

    EXPECT_TRUE(result.success);
    EXPECT_EQ(value_name, L"DesktopTodoList");
    EXPECT_EQ(command, L"\"C:/Program Files/DesktopTodoList.exe\"");
}

TEST_CASE(autostart_service_reports_registry_failure_without_claiming_enabled) {
    AutostartService service{{
        [](std::wstring_view, std::wstring_view) { return ERROR_ACCESS_DENIED; },
        [](std::wstring_view) { return ERROR_SUCCESS; }}};

    const auto result = service.set_enabled(true, L"C:/DesktopTodoList.exe");

    EXPECT_TRUE(!result.success);
    EXPECT_EQ(result.error, static_cast<unsigned long>(ERROR_ACCESS_DENIED));
}

