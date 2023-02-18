#include "Driver.h"

#define IOCTL_Custom_GetMonitorData CTL_CODE(FILE_DEVICE_SCREEN, 0x842, METHOD_BUFFERED, FILE_READ_ACCESS)

using namespace std;
using namespace PartialDisplay;

EVT_IDD_CX_DEVICE_IO_CONTROL PartialDisplayDeviceIoControl;

typedef NTSTATUS RequestHandler(WDFDEVICE Device, WDFREQUEST Request);
static RequestHandler HandleInvalid;
static RequestHandler HandleGetMonitorData;

_Use_decl_annotations_
VOID PartialDisplayDeviceIoControl(WDFDEVICE Device, WDFREQUEST Request, size_t, size_t, ULONG IoControlCode)
{
    NTSTATUS Status;
    RequestHandler* Handler;
    switch (IoControlCode)
    {
    case IOCTL_Custom_GetMonitorData:
        Handler = HandleGetMonitorData; break;
    default:
        Handler = HandleInvalid; break;
    }

    Status = Handler(Device, Request);
    if (NT_SUCCESS(Status))
    {
        WdfRequestCompleteWithInformation(Request, STATUS_SUCCESS, Status);
    }
    else
    {
        WdfRequestComplete(Request, Status);
    }
}

static NTSTATUS GetSwapChainProcessor(
    WDFDEVICE Device,
    UINT ConnectorIndex,
    SwapChainProcessor** ProcessorOut)
{
    IndirectDeviceContext* DeviceContext = WdfObjectGet_IndirectDeviceContextWrapper(Device)->pContext;
    IDDCX_MONITOR Monitor = DeviceContext->GetMonitorAt(ConnectorIndex);
    if (Monitor == nullptr)
    {
        return STATUS_INVALID_DEVICE_STATE;
    }
    IndirectMonitorContext* MonitorContext = WdfObjectGet_IndirectMonitorContextWrapper(Monitor)->pContext;
    SwapChainProcessor* Processor = MonitorContext->GetSwapChainProcessor();
    if (Processor == nullptr)
    {
        return STATUS_INVALID_DEVICE_STATE;
    }

    if (ProcessorOut) *ProcessorOut = Processor;
    return STATUS_SUCCESS;
}

static NTSTATUS HandleInvalid(WDFDEVICE, WDFREQUEST)
{
    return STATUS_INVALID_DEVICE_REQUEST;
}

static NTSTATUS HandleGetMonitorData(WDFDEVICE Device, WDFREQUEST Request)
{
    NTSTATUS Status;
    PVOID OutputBuffer;
    SwapChainProcessor* Processor;

    Status = GetSwapChainProcessor(Device, 0, &Processor);
    if (!NT_SUCCESS(Status)) return Status;

    size_t OutputBufferLength;
    Status = WdfRequestRetrieveOutputBuffer(Request, sizeof(UINT[3]), &OutputBuffer, &OutputBufferLength);
    if (!NT_SUCCESS(Status)) return Status;

    Status = Processor->FillRetrievalResponse(OutputBuffer, OutputBufferLength);
    return Status;
}