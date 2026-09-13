// ControlDevice.h: control device related definitions

#pragma once

EXTERN_C_START

NTSTATUS
PtpFilterCreateControlDevice(
    _In_ WDFDRIVER Driver
);

VOID
PtpFilterDeleteControlDevice(
    _In_ WDFOBJECT Context
);

//
// IOCTLs
//

#define PTPFILTER_CTL_CODE(x) CTL_CODE(FILE_DEVICE_UNKNOWN, x, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PTPFILTER_RELOAD_SETTINGS  PTPFILTER_CTL_CODE(0x800)
#define IOCTL_PTPFILTER_GET_BATTERY      PTPFILTER_CTL_CODE(0x801)
#define IOCTL_PTPFILTER_TRIGGER_HAPTIC   PTPFILTER_CTL_CODE(0x802)
#define IOCTL_PTPFILTER_SEND_RAW_HID     PTPFILTER_CTL_CODE(0x803)

#pragma pack(push, 1)
typedef struct _PTPFILTER_HAPTIC_PARAMS {
	UCHAR Waveform;  // e.g. 1
	UCHAR Intensity; // e.g. 1, 2, 3
	UCHAR Damping;   // e.g. 0, 1, 2
} PTPFILTER_HAPTIC_PARAMS, *PPTPFILTER_HAPTIC_PARAMS;

typedef struct _PTPFILTER_RAW_REPORT_PARAMS {
	UCHAR ReportType; // 0 = Feature Report, 1 = Output Report
	UCHAR ReportId;   // e.g. 0x53, 0xF2, etc.
	UCHAR Length;     // Payload data length (not including ReportId)
	UCHAR Data[64];   // Payload bytes
} PTPFILTER_RAW_REPORT_PARAMS, *PPTPFILTER_RAW_REPORT_PARAMS;
#pragma pack(pop)

EXTERN_C_END
