#include "presentation/accessibility_provider.h"

#include <oleacc.h>
#include <UIAutomation.h>

#include <algorithm>
#include <atomic>
#include <memory>
#include <new>
#include <utility>

namespace desktop_todo {
namespace {

AccessibleNode node(std::wstring id, std::wstring name, AccessibleRole role,
    std::wstring focused_id, bool selected = false) {
    return {id, id, std::move(name), role, true, id == focused_id, selected};
}

class FragmentProvider final : public IRawElementProviderSimple,
                               public IRawElementProviderFragment,
                               public IRawElementProviderFragmentRoot {
public:
    FragmentProvider(HWND window, std::shared_ptr<AccessibilityTree> tree,
        int index, FragmentProvider* root = nullptr)
        : window_(window), tree_(std::move(tree)), index_(index), root_(root) {
        if (root_ != nullptr) root_->AddRef();
    }

    ~FragmentProvider() { if (root_ != nullptr) root_->Release(); }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (object == nullptr) return E_POINTER;
        *object = nullptr;
        if (iid == IID_IUnknown || iid == IID_IRawElementProviderSimple)
            *object = static_cast<IRawElementProviderSimple*>(this);
        else if (iid == IID_IRawElementProviderFragment)
            *object = static_cast<IRawElementProviderFragment*>(this);
        else if (iid == IID_IRawElementProviderFragmentRoot && index_ == -1)
            *object = static_cast<IRawElementProviderFragmentRoot*>(this);
        else return E_NOINTERFACE;
        AddRef();
        return S_OK;
    }

    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const auto value = --references_;
        if (value == 0) delete this;
        return value;
    }

    HRESULT STDMETHODCALLTYPE get_ProviderOptions(ProviderOptions* options) override {
        if (!options) return E_POINTER;
        *options = ProviderOptions_ServerSideProvider;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID, IUnknown** provider) override {
        if (!provider) return E_POINTER;
        *provider = nullptr;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID property, VARIANT* value) override {
        if (!value) return E_POINTER;
        VariantInit(value);
        const auto& current = current_node();
        if (property == UIA_NamePropertyId) {
            value->vt = VT_BSTR;
            value->bstrVal = SysAllocString(current.name.c_str());
            return value->bstrVal ? S_OK : E_OUTOFMEMORY;
        }
        if (property == UIA_AutomationIdPropertyId) {
            value->vt = VT_BSTR;
            value->bstrVal = SysAllocString(current.id.c_str());
            return value->bstrVal ? S_OK : E_OUTOFMEMORY;
        }
        if (property == UIA_ControlTypePropertyId) {
            value->vt = VT_I4;
            value->lVal = control_type(current.role);
        } else if (property == UIA_IsEnabledPropertyId) {
            value->vt = VT_BOOL;
            value->boolVal = current.enabled ? VARIANT_TRUE : VARIANT_FALSE;
        } else if (property == UIA_IsKeyboardFocusablePropertyId) {
            value->vt = VT_BOOL;
            value->boolVal = index_ >= 0 ? VARIANT_TRUE : VARIANT_FALSE;
        } else if (property == UIA_HasKeyboardFocusPropertyId) {
            value->vt = VT_BOOL;
            value->boolVal = current.focused ? VARIANT_TRUE : VARIANT_FALSE;
        } else if (property == UIA_SelectionItemIsSelectedPropertyId) {
            value->vt = VT_BOOL;
            value->boolVal = current.selected ? VARIANT_TRUE : VARIANT_FALSE;
        } else if (property == UIA_ToggleToggleStatePropertyId) {
            value->vt = VT_I4;
            value->lVal = current.checked ? ToggleState_On : ToggleState_Off;
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_HostRawElementProvider(
        IRawElementProviderSimple** provider) override {
        if (!provider) return E_POINTER;
        return index_ == -1 ? UiaHostProviderFromHwnd(window_, provider) : (*provider = nullptr, S_OK);
    }

    HRESULT STDMETHODCALLTYPE Navigate(NavigateDirection direction,
        IRawElementProviderFragment** provider) override {
        if (!provider) return E_POINTER;
        *provider = nullptr;
        if (index_ == -1) {
            if (direction == NavigateDirection_FirstChild && !tree_->children.empty())
                return make_child(0, provider);
            if (direction == NavigateDirection_LastChild && !tree_->children.empty())
                return make_child(static_cast<int>(tree_->children.size() - 1), provider);
            return S_OK;
        }
        if (direction == NavigateDirection_Parent) {
            root_->AddRef();
            *provider = static_cast<IRawElementProviderFragment*>(root_);
            return S_OK;
        }
        const auto count = static_cast<int>(tree_->children.size());
        if (direction == NavigateDirection_NextSibling && index_ + 1 < count)
            return make_child(index_ + 1, provider);
        if (direction == NavigateDirection_PreviousSibling && index_ > 0)
            return make_child(index_ - 1, provider);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetRuntimeId(SAFEARRAY** runtime_id) override {
        if (!runtime_id) return E_POINTER;
        const auto& value = current_node().runtime_id;
        *runtime_id = SafeArrayCreateVector(VT_I4, 0, static_cast<ULONG>(value.size() + 2));
        if (!*runtime_id) return E_OUTOFMEMORY;
        LONG index = 0;
        LONG part = UiaAppendRuntimeId;
        SafeArrayPutElement(*runtime_id, &index, &part);
        ++index;
        part = 0x44544C;
        SafeArrayPutElement(*runtime_id, &index, &part);
        ++index;
        for (const auto character : value) {
            part = static_cast<LONG>(character);
            SafeArrayPutElement(*runtime_id, &index, &part);
            ++index;
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_BoundingRectangle(UiaRect* bounds) override {
        if (!bounds) return E_POINTER;
        if (index_ == -1) {
            RECT rect{};
            GetWindowRect(window_, &rect);
            *bounds = {static_cast<double>(rect.left), static_cast<double>(rect.top),
                static_cast<double>(rect.right - rect.left), static_cast<double>(rect.bottom - rect.top)};
        } else {
            const auto& rect = current_node().bounds;
            *bounds = {rect.x, rect.y, rect.width, rect.height};
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetEmbeddedFragmentRoots(SAFEARRAY** roots) override {
        if (!roots) return E_POINTER;
        *roots = nullptr;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetFocus() override { return ::SetFocus(window_) ? S_OK : S_FALSE; }
    HRESULT STDMETHODCALLTYPE get_FragmentRoot(IRawElementProviderFragmentRoot** provider) override {
        if (!provider) return E_POINTER;
        if (index_ == -1) AddRef(); else root_->AddRef();
        *provider = static_cast<IRawElementProviderFragmentRoot*>(index_ == -1 ? this : root_);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE ElementProviderFromPoint(double x, double y,
        IRawElementProviderFragment** provider) override {
        if (!provider) return E_POINTER;
        *provider = nullptr;
        for (std::size_t index = 0; index < tree_->children.size(); ++index) {
            const auto& rect = tree_->children[index].bounds;
            if (x >= rect.x && x < rect.right() && y >= rect.y && y < rect.bottom())
                return make_child(static_cast<int>(index), provider);
        }
        AddRef();
        *provider = static_cast<IRawElementProviderFragment*>(this);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetFocus(IRawElementProviderFragment** provider) override {
        if (!provider) return E_POINTER;
        *provider = nullptr;
        for (std::size_t index = 0; index < tree_->children.size(); ++index) {
            if (tree_->children[index].focused)
                return make_child(static_cast<int>(index), provider);
        }
        return S_OK;
    }

private:
    const AccessibleNode& current_node() const {
        return index_ == -1 ? *tree_ : tree_->children[static_cast<std::size_t>(index_)];
    }
    HRESULT make_child(int index, IRawElementProviderFragment** provider) {
        auto* child = new (std::nothrow) FragmentProvider(window_, tree_, index,
            index_ == -1 ? this : root_);
        if (!child) return E_OUTOFMEMORY;
        *provider = static_cast<IRawElementProviderFragment*>(child);
        return S_OK;
    }
    static int control_type(AccessibleRole role) {
        switch (role) {
        case AccessibleRole::window: return UIA_WindowControlTypeId;
        case AccessibleRole::button: return UIA_ButtonControlTypeId;
        case AccessibleRole::edit: return UIA_EditControlTypeId;
        case AccessibleRole::tab: return UIA_TabItemControlTypeId;
        case AccessibleRole::list_item: return UIA_ListItemControlTypeId;
        }
        return UIA_CustomControlTypeId;
    }

    std::atomic<ULONG> references_{1};
    HWND window_ = nullptr;
    std::shared_ptr<AccessibilityTree> tree_;
    int index_ = -1;
    FragmentProvider* root_ = nullptr;
};

}  // namespace

AccessibilityTree build_accessibility_tree(const AccessibilityTreeInput& input) {
    AccessibleNode tree;
    tree.id = L"desktop-todo-root";
    tree.runtime_id = tree.id;
    tree.name = input.window_name;
    tree.role = AccessibleRole::window;
    tree.children.push_back(node(L"new-task", L"新建任务", AccessibleRole::button, input.focused_id));
    tree.children.push_back(node(L"search", L"搜索待办", AccessibleRole::edit, input.focused_id));
    constexpr std::pair<std::wstring_view, ViewKind> tabs[]{
        {L"今日", ViewKind::today}, {L"本周", ViewKind::week},
        {L"全部", ViewKind::all}, {L"已完成", ViewKind::done}};
    for (const auto& [name, view] : tabs) {
        auto tab = node(L"view-" + std::to_wstring(static_cast<int>(view)),
            std::wstring{name}, AccessibleRole::tab, input.focused_id, view == input.current_view);
        tree.children.push_back(std::move(tab));
    }
    for (const auto& row : input.task_rows) {
        auto name = row.title;
        if (row.completed) name += L"，已完成";
        auto item = node(row.id, std::move(name), AccessibleRole::list_item,
            input.focused_id, row.selected);
        item.runtime_id = L"task:" + row.id;
        item.checked = row.completed;
        item.bounds = row.bounds;
        tree.children.push_back(std::move(item));
    }
    return tree;
}

std::vector<std::wstring> accessibility_focus_order(const AccessibilityTree& tree) {
    std::vector<std::wstring> order;
    order.reserve(tree.children.size());
    for (const auto& child : tree.children) order.push_back(child.id);
    return order;
}

AccessibilityProvider::AccessibilityProvider(HWND window, Snapshot snapshot)
    : window_(window), snapshot_(std::move(snapshot)) {}

LRESULT AccessibilityProvider::handle_get_object(WPARAM wparam, LPARAM lparam) const {
    if (static_cast<LONG>(lparam) != UiaRootObjectId || window_ == nullptr || !snapshot_) return 0;
    auto* provider = new (std::nothrow) FragmentProvider(window_,
        std::make_shared<AccessibilityTree>(snapshot_()), -1);
    if (!provider) return 0;
    const auto result = UiaReturnRawElementProvider(window_, wparam, lparam, provider);
    provider->Release();
    return result;
}

}  // namespace desktop_todo
