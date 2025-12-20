#include <windows.h>
#include <string>
#include <fstream>
#include <filesystem>

// =============================
//  Config
// =============================
static constexpr wchar_t SERVICE_NAME[] = L"DlpAgentService";

// =============================
//  Logging
// =============================
static void WriteLog(const std::wstring& msg)
{
    try {
        std::filesystem::create_directories(LR"(C:\DLP)");
        std::wofstream ofs(LR"(C:\DLP\agent.log)", std::ios::app);
        if (!ofs.is_open()) return;

        SYSTEMTIME st{};
        GetLocalTime(&st);

        ofs << L"["
            << st.wYear << L"-"
            << st.wMonth << L"-"
            << st.wDay << L" "
            << st.wHour << L":"
            << st.wMinute << L":"
            << st.wSecond
            << L"] " << msg << L"\n";
    }
    catch (...) {
        // ignore
    }
}

// =============================
//  Service globals
// =============================
static SERVICE_STATUS          g_ServiceStatus{};
static SERVICE_STATUS_HANDLE   g_StatusHandle = nullptr;
static HANDLE                  g_StopEvent = nullptr;

// Forward decl
static void WINAPI ServiceMain(DWORD argc, LPWSTR* argv);
static DWORD WINAPI ServiceCtrlHandlerEx(DWORD control, DWORD eventType, void* eventData, void* context);
static void SetSvcStatus(DWORD currentState, DWORD win32ExitCode, DWORD waitHintMs);

// =============================
//  Helper: update SCM status
// =============================
static void SetSvcStatus(DWORD currentState, DWORD win32ExitCode, DWORD waitHintMs)
{
    g_ServiceStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    g_ServiceStatus.dwCurrentState = currentState;
    g_ServiceStatus.dwWin32ExitCode = win32ExitCode;
    g_ServiceStatus.dwWaitHint = waitHintMs;

    // accepted controls depend on state
    if (currentState == SERVICE_START_PENDING) {
        g_ServiceStatus.dwControlsAccepted = 0;
        g_ServiceStatus.dwCheckPoint++;
    }
    else {
        g_ServiceStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;
        if (currentState == SERVICE_RUNNING || currentState == SERVICE_STOPPED) {
            g_ServiceStatus.dwCheckPoint = 0;
        }
        else {
            g_ServiceStatus.dwCheckPoint++;
        }
    }

    if (g_StatusHandle) {
        ::SetServiceStatus(g_StatusHandle, &g_ServiceStatus);
    }
}

// =============================
//  Service control handler
// =============================
static DWORD WINAPI ServiceCtrlHandlerEx(DWORD control, DWORD /*eventType*/, void* /*eventData*/, void* /*context*/)
{
    switch (control) {
    case SERVICE_CONTROL_STOP:
    case SERVICE_CONTROL_SHUTDOWN:
        WriteLog(L"CONTROL: STOP/SHUTDOWN received");
        if (g_StopEvent) {
            SetSvcStatus(SERVICE_STOP_PENDING, NO_ERROR, 3000);
            ::SetEvent(g_StopEvent);
        }
        return NO_ERROR;

    default:
        return NO_ERROR;
    }
}

// =============================
//  Service entry
// =============================
static void WINAPI ServiceMain(DWORD /*argc*/, LPWSTR* /*argv*/)
{
    g_StatusHandle = ::RegisterServiceCtrlHandlerExW(SERVICE_NAME, ServiceCtrlHandlerEx, nullptr);
    if (!g_StatusHandle) {
        WriteLog(L"ERROR: RegisterServiceCtrlHandlerExW failed");
        return;
    }

    // init status struct
    g_ServiceStatus = {};
    g_ServiceStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    g_ServiceStatus.dwCheckPoint = 1;

    SetSvcStatus(SERVICE_START_PENDING, NO_ERROR, 3000);
    WriteLog(L"SERVICE: START_PENDING");

    g_StopEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!g_StopEvent) {
        WriteLog(L"ERROR: CreateEventW failed");
        SetSvcStatus(SERVICE_STOPPED, GetLastError(), 0);
        return;
    }

    // TODO: 여기에 정책 로드 / USB 감지 / 프로세스 감사 초기화를 나중에 붙이면 됨
    WriteLog(L"SERVICE: initialization done");

    SetSvcStatus(SERVICE_RUNNING, NO_ERROR, 0);
    WriteLog(L"SERVICE: RUNNING");

    // Main loop: wait until stop
    ::WaitForSingleObject(g_StopEvent, INFINITE);

    WriteLog(L"SERVICE: stopping...");
    SetSvcStatus(SERVICE_STOP_PENDING, NO_ERROR, 3000);

    if (g_StopEvent) {
        ::CloseHandle(g_StopEvent);
        g_StopEvent = nullptr;
    }

    SetSvcStatus(SERVICE_STOPPED, NO_ERROR, 0);
    WriteLog(L"SERVICE: STOPPED");
}

// =============================
//  Process entry (must exist)
// =============================
int wmain()
{
    // Service Table
    SERVICE_TABLE_ENTRYW serviceTable[] = {
        { const_cast<LPWSTR>(SERVICE_NAME), ServiceMain },
        { nullptr, nullptr }
    };

    // Connect to SCM. If this fails and you want debugging, run in console mode separately.
    if (!::StartServiceCtrlDispatcherW(serviceTable)) {
        DWORD err = ::GetLastError();

        // 1063 = not started as a service (e.g., you ran exe directly)
        if (err == ERROR_FAILED_SERVICE_CONTROLLER_CONNECT) {
            WriteLog(L"NOTE: Started as console (not via SCM). This exe is intended to run as a service.");
        }
        else {
            WriteLog(L"ERROR: StartServiceCtrlDispatcherW failed: " + std::to_wstring(err));
        }
        return static_cast<int>(err);
    }

    return 0;
}
