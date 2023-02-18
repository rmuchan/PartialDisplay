#include "App.h"
#include <thread>
#include <conio.h>

using namespace std;
using namespace PartialDisplay;

struct MonitorEnumData
{
    HMONITOR selected;
    LONG x;
    LONG y;
};

BOOL MonitorEnumProc(HMONITOR hMonitor, HDC, LPRECT lpRect, LPARAM lParam)
{
    auto data = reinterpret_cast<MonitorEnumData*>(lParam);
    if (data->selected == nullptr || lpRect->left > data->x || lpRect->top > data->y)
    {
        data->selected = hMonitor;
        data->x = lpRect->left;
        data->y = lpRect->top;
    }
    return true;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    SetProcessDPIAware();

    Ioctl ioctl;
    if (!ioctl.CreateDevice()) { return 1; }
    if (!ioctl.GetDeviceFileName()) { return 1; }

    Sleep(1500);

    bool handleOpened = false;
    for (int retry = 0; retry < 10; retry++)
    {
        Sleep(500);
        if (ioctl.TryOpenHandle())
        {
            handleOpened = true;
            break;
        }
    }

    if (!handleOpened) { return 1; }

    MonitorEnumData data = {};
    EnumDisplayMonitors(nullptr, nullptr, MonitorEnumProc, (LPARAM)&data);

    auto window = Window::CreateMyWindow(hInstance, data.selected);
    if (!window) { return 1; }

    bool rendering = true;
    thread renderingThread([&rendering, &window, &ioctl]
        {
            while (rendering)
            {
                if (!window->UpdateFrame(ioctl))
                {
                    this_thread::sleep_for(1s);
                }
            }
        });

    int ret = window->MainLoop();
    rendering = false;
    renderingThread.join();

    return ret;
}