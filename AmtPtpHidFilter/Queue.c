// Queue.c: IO-queue related operations

#include <Driver.h>
#include "Queue.tmh"

#ifdef ALLOC_PRAGMA
#pragma alloc_text (PAGE, PtpFilterIoQueueInitialize)
#endif

NTSTATUS
PtpFilterIoQueueInitialize(
    _In_ WDFDEVICE Device
)
{
    WDFQUEUE queue;
    WDF_IO_QUEUE_CONFIG queueConfig;
    WDF_OBJECT_ATTRIBUTES queueAttributes;
    PDEVICE_CONTEXT deviceContext;
    PQUEUE_CONTEXT queueContext;
    NTSTATUS status;

    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Entry");

    deviceContext = PtpFilterGetContext(Device);

    // First queue for system-wide HID controls
    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&queueConfig, WdfIoQueueDispatchParallel);
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&queueAttributes, QUEUE_CONTEXT);
    queueConfig.EvtIoInternalDeviceControl = FilterEvtIoIntDeviceControl;
    queueConfig.EvtIoStop = FilterEvtIoStop;
    status = WdfIoQueueCreate(Device, &queueConfig, &queueAttributes, &queue);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_QUEUE, "%!FUNC! WdfIoQueueCreate failed %!STATUS!", status);
        goto exit;
    }

    queueContext = PtpFilterQueueGetContext(queue);
    queueContext->Device = deviceContext->Device;
    queueContext->DeviceIoTarget = deviceContext->HidIoTarget;

    // Second queue for HID read requests
    WDF_IO_QUEUE_CONFIG_INIT(&queueConfig, WdfIoQueueDispatchManual);
    queueConfig.PowerManaged = WdfFalse;
    status = WdfIoQueueCreate(Device, &queueConfig, WDF_NO_OBJECT_ATTRIBUTES, &deviceContext->HidReadQueue);
    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_QUEUE, "%!FUNC! WdfIoQueueCreate (Input) failed %!STATUS!", status);
    }

exit:
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Exit, Status = %!STATUS!", status);
    return status;
}

VOID
FilterEvtIoIntDeviceControl(
    _In_ WDFQUEUE Queue,
    _In_ WDFREQUEST Request,
    _In_ size_t OutputBufferLength,
    _In_ size_t InputBufferLength,
    _In_ ULONG IoControlCode
)
{
    PQUEUE_CONTEXT queueContext;
    PDEVICE_CONTEXT deviceContext;
    BOOLEAN requestPending = FALSE;
	NTSTATUS status = STATUS_UNSUCCESSFUL;
    
    UNREFERENCED_PARAMETER(InputBufferLength);
    UNREFERENCED_PARAMETER(OutputBufferLength);

    queueContext = PtpFilterQueueGetContext(Queue);
    deviceContext = PtpFilterGetContext(queueContext->Device);

	switch (IoControlCode)
	{
	case IOCTL_HID_GET_DEVICE_DESCRIPTOR:
		status = PtpFilterGetHidDescriptor(queueContext->Device, Request);
		break;
	case IOCTL_HID_GET_DEVICE_ATTRIBUTES:
		status = PtpFilterGetDeviceAttribs(queueContext->Device, Request);
		break;
	case IOCTL_HID_GET_REPORT_DESCRIPTOR:
		status = PtpFilterGetReportDescriptor(queueContext->Device, Request);
		break;
	case IOCTL_HID_GET_STRING:
		status = PtpFilterGetStrings(queueContext->Device, Request, &requestPending);
		break;
	case IOCTL_HID_READ_REPORT:
        PtpFilterInputProcessRequest(queueContext->Device, Request);
		requestPending = TRUE;
		break;
	case IOCTL_HID_GET_FEATURE:
		status = PtpFilterGetHidFeatures(queueContext->Device, Request);
		break;
	case IOCTL_HID_SET_FEATURE:
		status = PtpFilterSetHidFeatures(queueContext->Device, Request);
		break;
	case IOCTL_HID_WRITE_REPORT:
	case IOCTL_UMDF_HID_SET_OUTPUT_REPORT:
	{
		PHID_XFER_PACKET hidPacket = NULL;
		PIRP irp = WdfRequestWdmGetIrp(Request);
		if (irp != NULL) {
			PIO_STACK_LOCATION irpSp = IoGetCurrentIrpStackLocation(irp);
			if (irpSp != NULL && irpSp->Parameters.DeviceIoControl.Type3InputBuffer != NULL) {
				hidPacket = (PHID_XFER_PACKET)irpSp->Parameters.DeviceIoControl.Type3InputBuffer;
			}
			if (hidPacket == NULL) {
				hidPacket = (PHID_XFER_PACKET)irp->UserBuffer;
			}
		}
		if (hidPacket == NULL) {
			size_t inLen = 0;
			WdfRequestRetrieveInputBuffer(Request, sizeof(HID_XFER_PACKET), (PVOID*)&hidPacket, &inLen);
		}

		if (hidPacket != NULL && hidPacket->reportId == REPORTID_HAPTIC_TRIGGER && hidPacket->reportBuffer != NULL && hidPacket->reportBufferLen >= 2) {
			UCHAR manualTrigger = hidPacket->reportBuffer[1];
			UCHAR intensity = (hidPacket->reportBufferLen >= 3) ? hidPacket->reportBuffer[2] : 2;
			UCHAR waveform = 0x01;
			UCHAR damping = 0x00;

			// Map Microsoft Haptic Waveform ordinals:
			// Ordinals 1=None, 2=Stop
			// Ordinal 3=Hover, 4=Collide, 5=Align (Snap), 6=Step, 7=Grow
			if (manualTrigger >= 3) {
				switch (manualTrigger) {
				case 3: // Hover
					waveform = 0x01;
					damping = 0x02;
					if (intensity == 0) intensity = 1;
					break;
				case 4: // Collide
					waveform = 0x01;
					damping = 0x01;
					if (intensity == 0) intensity = 2;
					break;
				case 5: // Align (Snap Assist)
					waveform = 0x01;
					damping = 0x00;
					if (intensity == 0) intensity = 3;
					break;
				case 6: // Step
					waveform = 0x01;
					damping = 0x00;
					if (intensity == 0) intensity = 2;
					break;
				case 7: // Grow
					waveform = 0x02;
					damping = 0x01;
					if (intensity == 0) intensity = 3;
					break;
				default:
					waveform = 0x01;
					damping = 0x00;
					break;
				}

				PtpFilterTriggerActuatorPulseSafe(queueContext->Device, waveform, intensity, damping);
			}
			status = STATUS_SUCCESS;
		} else {
			status = STATUS_NOT_SUPPORTED;
			TraceEvents(TRACE_LEVEL_WARNING, TRACE_QUEUE, "%!FUNC!: %s unhandled report %d",
				PtpFilterDiagnosticsIoControlGetString(IoControlCode),
				hidPacket ? hidPacket->reportId : -1);
		}
		break;
	}
	case IOCTL_UMDF_HID_GET_INPUT_REPORT:
	case IOCTL_HID_ACTIVATE_DEVICE:
	case IOCTL_HID_DEACTIVATE_DEVICE:
	case IOCTL_HID_SEND_IDLE_NOTIFICATION_REQUEST:
	default:
		status = STATUS_NOT_SUPPORTED;
		TraceEvents(TRACE_LEVEL_WARNING, TRACE_QUEUE, "%!FUNC!: %s is not yet implemented", PtpFilterDiagnosticsIoControlGetString(IoControlCode));
		break;
	}

    if (requestPending != TRUE)
    {
        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_QUEUE, "%!FUNC!: %s, Status = %!STATUS!", PtpFilterDiagnosticsIoControlGetString(IoControlCode), status);
        WdfRequestComplete(Request, status);
    }
}

VOID
FilterEvtIoStop(
    _In_ WDFQUEUE Queue,
    _In_ WDFREQUEST Request,
    _In_ ULONG ActionFlags
)
{
    UNREFERENCED_PARAMETER(Queue);
    UNREFERENCED_PARAMETER(Request);
    UNREFERENCED_PARAMETER(ActionFlags);
}
