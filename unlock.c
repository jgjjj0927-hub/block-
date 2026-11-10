#include <windows.h>
#include <stdio.h>

// Экспортируемая функция для снятия блокировки
extern "C" __declspec(dllexport) BOOL UnlockSystem() {
    SC_HANDLE schSCManager = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!schSCManager) return FALSE;

    // Останавливаем драйвер
    SC_HANDLE schService = OpenService(schSCManager, L"blocker", SERVICE_STOP | DELETE);
    if (schService) {
        SERVICE_STATUS ss;
        ControlService(schService, SERVICE_CONTROL_STOP, &ss);
        DeleteService(schService);
        CloseServiceHandle(schService);
    }

    CloseServiceHandle(schSCManager);
    return TRUE;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        UnlockSystem();
    }
    return TRUE;
}
