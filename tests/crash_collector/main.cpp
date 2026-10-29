// Disposable native collector fixture only. Never shipped as the product.
#include <windows.h>
#include <shellapi.h>
#include <fstream>
#include <filesystem>
#include <cstdint>
std::filesystem::path trace;
static void record(const char* event) {
    std::ofstream out(trace,std::ios::app);
    out << "{\"pid\":" << GetCurrentProcessId() << ",\"event\":\"" << event << "\"}\n";
}
__declspec(noinline) void fixtureCrash() {
    record("synthetic-crash-requested");
    *reinterpret_cast<volatile int*>(static_cast<uintptr_t>(1))=42;
}
static LRESULT CALLBACK windowProcedure(HWND window,UINT message,WPARAM w,LPARAM l) {
    if (message==WM_APP+1) fixtureCrash();
    if (message==WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(window,message,w,l);
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int show) {
    wchar_t allowed[8]{};
    if (GetEnvironmentVariableW(L"LUNA_SYNTHETIC_CRASH_FIXTURE",allowed,8)!=1 || allowed[0]!=L'1') return 2;
    int count=0; auto** arguments=CommandLineToArgvW(GetCommandLineW(),&count);
    for (int i=1;i+1<count;++i) if (std::wstring(arguments[i])==L"--luna-lifecycle-trace") trace=arguments[++i];
    LocalFree(arguments); if (trace.empty()) return 3;
    record("synthetic-fixture-start");
    WNDCLASSW type{}; type.hInstance=instance; type.lpfnWndProc=windowProcedure; type.lpszClassName=L"LunaCollectorSyntheticFixture";
    if (!RegisterClassW(&type)) return 4;
    auto window=CreateWindowW(type.lpszClassName,L"LUNA collector synthetic fixture - no PDF",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,400,240,nullptr,nullptr,instance,nullptr);
    if (!window) return 5; ShowWindow(window,show);
    MSG message{}; while (GetMessageW(&message,nullptr,0,0)>0) { TranslateMessage(&message); DispatchMessageW(&message); }
    return 0;
}
