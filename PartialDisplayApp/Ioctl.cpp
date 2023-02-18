#include "App.h"
#include <winioctl.h>

#define INITGUID
#include <devpkey.h>

using namespace std;
using namespace PartialDisplay;
using namespace PartialDisplay::Helper;

#define IOCTL_Custom_GetMonitorData CTL_CODE(FILE_DEVICE_SCREEN, 0x842, METHOD_BUFFERED, FILE_READ_ACCESS)

struct CreateCallbackArguments
{
    bool Cancelled;
    Ioctl& Caller;
    unique_handle EvtFinished;
    HRESULT CreateResult;

    CreateCallbackArguments(Ioctl& Caller) :
        Cancelled(false),
        Caller(Caller),
        EvtFinished(CreateEvent(nullptr, FALSE, FALSE, nullptr)),
        CreateResult(S_FALSE) {}
};

bool Ioctl::CreateDevice()
{
    HSWDEVICE hSwDevice;
    PCWSTR description = L"Partial Display Device";

    // These match the Pnp id's in the inf file so OS will load the driver when the device is created    
    PCWSTR instanceId = L"PartialDisplay";
    PCWSTR hardwareIds = L"PartialDisplay\0\0";
    PCWSTR compatibleIds = L"PartialDisplay\0\0";

    SW_DEVICE_CREATE_INFO createInfo = {};
    createInfo.cbSize = sizeof(createInfo);
    createInfo.pszzCompatibleIds = compatibleIds;
    createInfo.pszInstanceId = instanceId;
    createInfo.pszzHardwareIds = hardwareIds;
    createInfo.pszDeviceDescription = description;
    createInfo.CapabilityFlags = SWDeviceCapabilitiesRemovable |
        SWDeviceCapabilitiesSilentInstall |
        SWDeviceCapabilitiesDriverRequired;

    // Create the device
    auto cbarg = std::make_unique<CreateCallbackArguments>(*this);
    HRESULT hr = SwDeviceCreate(L"PartialDisplay", L"HTREE\\ROOT\\0", &createInfo,
        0, nullptr, SwDeviceCreationCallback, cbarg.get(), &hSwDevice);
    if (FAILED(hr))
    {
        printf("SwDeviceCreate failed with 0x%lx\n", hr);
        return false;
    }

    // Wait for callback to signal that the device has been created
    printf("Waiting for device to be created....\n");
    DWORD waitResult = WaitForSingleObject(cbarg->EvtFinished.Get(), 10 * 1000);
    if (waitResult != WAIT_OBJECT_0)
    {
        printf("Wait for device creation failed\n");
        cbarg.release()->Cancelled = true;  // Let the callback delete it.
        return false;
    }
    if (FAILED(cbarg->CreateResult))
    {
        printf("Device creation failed with 0x%lx\n", cbarg->CreateResult);
        SwDeviceClose(hSwDevice);
        return false;
    }

    printf("Device created\n\n");
    m_hSwDevice.Attach(hSwDevice);
    return true;
}

void Ioctl::SwDeviceCreationCallback(HSWDEVICE hSwDevice, HRESULT CreateResult, PVOID pContext, PCWSTR pszDeviceInstanceId)
{
    auto args = static_cast<CreateCallbackArguments*>(pContext);
    if (!args || args->Cancelled)  // args will not be null, but make IDE happy
    {
        delete args;
    }
    else
    {
        args->CreateResult = CreateResult;
        wcscpy_s(args->Caller.m_DeviceInstanceId, (SUCCEEDED(CreateResult) && pszDeviceInstanceId) ? pszDeviceInstanceId : L"");
        SetEvent(args->EvtFinished.Get());
    }
}

bool Ioctl::GetDeviceFileName()
{
    // Get device instance handle from instance id
    DEVINST devInst;
    CONFIGRET cmret = CM_Locate_DevNode(&devInst, m_DeviceInstanceId, CM_LOCATE_DEVNODE_NORMAL);
    if (cmret != CR_SUCCESS)
    {
        printf("Can't locate device node: %#lx\n", cmret);
        return false;
    }

    // Get device PDO name
    DEVPROPTYPE PropertyType;
    ULONG BufferSize = 0;
    cmret = CM_Get_DevNode_Property(devInst, &DEVPKEY_Device_PDOName, &PropertyType, nullptr, &BufferSize, 0);
    if (cmret != CR_BUFFER_SMALL)
    {
        printf("Can't get PDO name size: %#lx\n", cmret);
        return false;
    }
    auto Buffer = make_unique<BYTE[]>(BufferSize);
    cmret = CM_Get_DevNode_Property(devInst, &DEVPKEY_Device_PDOName, &PropertyType, Buffer.get(), &BufferSize, 0);
    if (cmret != CR_SUCCESS)
    {
        printf("Can't get PDO name: %#lx\n", cmret);
        return false;
    }

    m_DeviceFileName = wstring(L"\\\\?\\GLOBALROOT") + reinterpret_cast<PWSTR>(Buffer.get());
    printf("File name is %ws\n", m_DeviceFileName.c_str());
    return true;
}

bool Ioctl::TryOpenHandle()
{
    HANDLE hDevice = CreateFile(m_DeviceFileName.c_str(), GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (hDevice == INVALID_HANDLE_VALUE)
    {
        DWORD error = GetLastError();
        printf("Can't open device: %#lx\n", error);
        return false;
    }

    m_hDevice.Attach(hDevice);
    return true;
}

bool Ioctl::RefreshMonitorData()
{
    for (int retry = 0; retry < 3; retry++)
    {
        DWORD Returned;
        if (!DeviceIoControl(m_hDevice.Get(), IOCTL_Custom_GetMonitorData, nullptr, 0,
            m_Monitor.Buffer.data(), m_Monitor.Buffer.size(), &Returned, nullptr))
        {
            DWORD error = GetLastError();
            printf("IOCTL Error: %lx\n", error);
            return false;
        }

        if (Returned > m_Monitor.HeaderLength)
        {
            m_Monitor.Buffer.resize(Returned);
            return true;
        }

        size_t required = m_Monitor.HeaderLength + size_t(m_Monitor.GetPitch()) * m_Monitor.GetHeight();
        m_Monitor.Buffer.resize(required);
    }

    printf("Continuous small buffer.\n");
    return false;
}