#include "test_support.h"
#include "domain/build_info.h"

TEST_CASE(build_marker_identifies_native_core) {
    EXPECT_EQ(desktop_todo::build_marker(), L"DesktopTodoList.Native");
}

int main() {
    return desktop_todo::test_support::run_all();
}
