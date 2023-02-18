#include "App.h"

using namespace std;
using namespace PartialDisplay;

static weak_ptr<Window> s_Instance;

Window::Window() : m_hWnd()
{
}

Window::~Window()
{
    if (m_hWnd != nullptr)
    {
        CreateMyTray(m_hWnd, false);
    }
}

shared_ptr<Window> Window::CreateMyWindow(HINSTANCE hInstance, HMONITOR hMonitor)
{
    if (!s_Instance.expired())
    {
        printf("Window already created.\n");
        return nullptr;
    }

    auto instance = make_shared<Window>();
    s_Instance = instance;
    if (!instance->InitMyWindow(hInstance, hMonitor))
    {
        return nullptr;
    }

    return instance;
}

bool Window::InitMyWindow(HINSTANCE hInstance, HMONITOR hMonitor)
{
    WNDCLASSEX wc = {};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = L"WindowClass";
    ATOM cls = RegisterClassEx(&wc);
    if (cls == 0) { return false; }

    MONITORINFO mi = {};
    mi.cbSize = sizeof(MONITORINFO);
    if (!GetMonitorInfo(hMonitor, &mi)) { return false; }

    RECT& wr = mi.rcMonitor;

    m_hWnd = CreateWindowEx(
        WS_EX_TOOLWINDOW,  // hide from alt-tab
        (LPCWSTR)cls,
        L"Partial Display",
        WS_POPUP,
        wr.left,
        wr.top,
        wr.right - wr.left,
        wr.bottom - wr.top,
        nullptr,
        nullptr,
        hInstance,
        nullptr);

    if (m_hWnd == nullptr)
    {
        printf("Creating window failed with %#lx\n", GetLastError());
        return false;
    }

    m_Rendering.emplace();
    if (FAILED(m_Rendering->InitD3D(m_hWnd, wr.right - wr.left, wr.bottom - wr.top)))
    {
        printf("D3D init failure.\n");
        m_Rendering.reset();
        return false;
    }

    ShowWindow(m_hWnd, SW_MAXIMIZE);

    return true;
}

int Window::MainLoop()
{
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return (int)msg.wParam;
}

bool Window::UpdateFrame(Ioctl& ioctl)
{
    return m_Rendering.has_value()
        && ioctl.RefreshMonitorData()
        && SUCCEEDED(m_Rendering->UpdateFrame(ioctl.m_Monitor.GetWidth(), ioctl.m_Monitor.GetHeight(),
            ioctl.m_Monitor.GetPitch(), ioctl.m_Monitor.GetData()));
}

LRESULT CALLBACK Window::WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    auto instance = s_Instance.lock();
    if (instance == nullptr)
    {
        return DefWindowProc(hWnd, message, wParam, lParam);
    }

    static const UINT WmTaskbarCreated = RegisterWindowMessage(L"TaskbarCreated");

    switch (message)
    {
    case WM_CREATE:
        instance->CreateMyTray(hWnd, true);
        return 0;

    case WM_SIZE:
        if (instance->m_Rendering)
        {
            instance->m_Rendering->OnSize(LOWORD(lParam), HIWORD(lParam));
        }
        return 0;

    case WM_USER:
        instance->HandleTrayMessage(wParam, lParam);
        return 0;

    case WM_DESTROY:
        s_Instance.reset();
        PostQuitMessage(0);
        return 0;

    default:
        if (message == WmTaskbarCreated)
        {
            instance->CreateMyTray(hWnd, true);
            return 0;
        }

        return DefWindowProc(hWnd, message, wParam, lParam);
    }
}

bool Window::CreateMyTray(HWND hWnd, bool create)
{
    NOTIFYICONDATA m_Nid = {};
    
    m_Nid.cbSize = sizeof(NOTIFYICONDATA);
    m_Nid.hWnd = hWnd;
    m_Nid.uID = 0;
    m_Nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    m_Nid.uCallbackMessage = WM_USER;
    m_Nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wcscpy_s(m_Nid.szTip, L"Partial Display");
    
    return Shell_NotifyIcon(create ? NIM_ADD : NIM_DELETE, &m_Nid);
}

bool Window::HandleTrayMessage(WPARAM wParam, LPARAM lParam)
{
    if (lParam == WM_RBUTTONDOWN)
    {
        HandleT<Helper::HMENU_Traits> hMenu(CreatePopupMenu());
        if (!hMenu.IsValid()) { return false; }
        AppendMenu(hMenu.Get(), MF_STRING, 1, L"Exit");

        POINT cursor = {};
        GetCursorPos(&cursor);
        SetForegroundWindow(m_hWnd);

        int selection = TrackPopupMenu(hMenu.Get(), TPM_RETURNCMD, cursor.x, cursor.y, 0, m_hWnd, nullptr);
        if (selection == 1)
        {
            PostQuitMessage(0);
            return true;
        }

        return true;
    }
    if (lParam == WM_LBUTTONDOWN)
    {
        return SetForegroundWindow(m_hWnd);
    }

    return true;
}