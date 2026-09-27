#include "test_support.h"
#include "domain/build_info.h"

TEST_CASE(build_marker_identifies_native_core) {
    EXPECT_EQ(desktop_todo::build_marker(), L"DesktopTodoList.Native");
}

int main(int argc, char** argv) {
    const std::string_view filter = argc == 3 && std::string_view{argv[1]} == "--filter"
        ? std::string_view{argv[2]}
        : std::string_view{};
    return desktop_todo::test_support::run_all(filter);
}
