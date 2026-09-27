#include "domain/build_info.h"

namespace desktop_todo {

std::wstring_view build_marker() noexcept {
    return L"DesktopTodoList.Native";
}

}  // namespace desktop_todo
