#include <windows.h>

#include "PluginInterface.h"

#include <cstdio>
#include <string>

static std::string ToUtf8(const wchar_t* text)
{
    if (text == nullptr) {
        return "(null)";
    }
    const int length = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (length <= 0) {
        return "";
    }
    std::string result(static_cast<size_t>(length - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, result.data(), length, nullptr, nullptr);
    return result;
}

int main()
{
    HMODULE module = LoadLibraryW(L"build\\DeepSeekUsage.dll");
    if (module == nullptr) {
        std::printf("LoadLibrary failed: %lu\n", GetLastError());
        return 1;
    }

    using GetInstanceFn = ITMPlugin* (*)();
    auto get_instance = reinterpret_cast<GetInstanceFn>(
        GetProcAddress(module, "TMPluginGetInstance"));
    if (get_instance == nullptr) {
        std::printf("GetProcAddress failed: %lu\n", GetLastError());
        FreeLibrary(module);
        return 1;
    }

    ITMPlugin* plugin = get_instance();
    if (plugin == nullptr) {
        std::printf("TMPluginGetInstance returned null\n");
        FreeLibrary(module);
        return 1;
    }

    std::printf("API version: %d\n", plugin->GetAPIVersion());
    std::printf("name: %s\n", ToUtf8(plugin->GetInfo(ITMPlugin::TMI_NAME)).c_str());
    std::printf("version: %s\n", ToUtf8(plugin->GetInfo(ITMPlugin::TMI_VERSION)).c_str());
    std::printf("tooltip: %s\n", ToUtf8(plugin->GetTooltipInfo()).c_str());

    for (int index = 0; index < 5; ++index) {
        IPluginItem* item = plugin->GetItem(index);
        if (item == nullptr) {
            std::printf("item[%d]: null\n", index);
            break;
        }
        std::printf("item[%d]: name=%s id=%s label=%s value=%s sample=%s\n",
                    index,
                    ToUtf8(item->GetItemName()).c_str(),
                    ToUtf8(item->GetItemId()).c_str(),
                    ToUtf8(item->GetItemLableText()).c_str(),
                    ToUtf8(item->GetItemValueText()).c_str(),
                    ToUtf8(item->GetItemValueSampleText()).c_str());
    }

    plugin->DataRequired();

    FreeLibrary(module);
    return 0;
}
