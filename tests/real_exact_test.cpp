#include <windows.h>

#include <cstdio>
#include <string>

namespace {

std::string ToUtf8(const std::wstring& text)
{
    if (text.empty()) {
        return "";
    }
    const int length = WideCharToMultiByte(CP_UTF8, 0, text.c_str(),
                                           static_cast<int>(text.size()),
                                           nullptr, 0, nullptr, nullptr);
    if (length <= 0) {
        return "";
    }
    std::string result(static_cast<size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                        result.data(), length, nullptr, nullptr);
    return result;
}

} // namespace

int main()
{
    const std::wstring token_path = L"build\\token_normalized.txt";
    HANDLE file = CreateFileW(token_path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        std::printf("token file missing: %lu\n", GetLastError());
        return 1;
    }

    LARGE_INTEGER size{};
    GetFileSizeEx(file, &size);
    std::string token(static_cast<size_t>(size.QuadPart), '\0');
    DWORD read = 0;
    ReadFile(file, token.data(), static_cast<DWORD>(token.size()), &read, nullptr);
    CloseHandle(file);
    token.resize(read);
    while (!token.empty() &&
           (token.back() == '\r' || token.back() == '\n' || token.back() == ' ' ||
            token.back() == '\t')) {
        token.pop_back();
    }

    HMODULE module = LoadLibraryW(L"build\\DeepSeekUsage_test.dll");
    if (module == nullptr) {
        std::printf("LoadLibrary failed: %lu\n", GetLastError());
        return 1;
    }

    using GetPluginFn = void* (*)();
    using TestExactFn = int (*)(void*, const wchar_t*, const wchar_t*, int, wchar_t*, int);
    auto get_plugin = reinterpret_cast<GetPluginFn>(
        GetProcAddress(module, "TMTestGetPlugin"));
    auto test_exact = reinterpret_cast<TestExactFn>(
        GetProcAddress(module, "TMTestExact"));
    if (get_plugin == nullptr || test_exact == nullptr) {
        std::printf("test hooks missing\n");
        return 1;
    }

    void* plugin = get_plugin();
    const std::wstring wide_token(token.begin(), token.end());
    wchar_t message[2048]{};
    const int ok = test_exact(plugin, wide_token.c_str(), L"CNY", 0, message, 2048);
    std::printf("result=%d\n%s\n", ok, ToUtf8(message).c_str());
    return ok ? 0 : 2;
}
