// Exact-instance driver selection using Microsoft's preinstalled packages.
// Does not create certificates, change hardware IDs, or edit device filters.
#include <windows.h>
#include <setupapi.h>
#include <newdev.h>
#include <iostream>
#include <string>
#include <vector>
#include "controller_profiles.h"

static int fail(const char* operation) {
    std::cerr << "[DRIVER] " << operation << " failed win32=" << GetLastError() << '\n';
    return 1;
}
int wmain(int argc, wchar_t** argv) {
    if (argc != 5 || (std::wstring(argv[1]) != L"list" && std::wstring(argv[1]) != L"install")) {
        std::cerr << "Usage: ax201_driver list|install <exact-instance> <preinstalled-INF> <section>\n";
        return 64;
    }
    const std::wstring instance = argv[2], inf = argv[3], section = argv[4];
    if (!hfp_profile_for_instance(instance.c_str()) || inf.size() >= MAX_PATH) return 64;
    const bool install = std::wstring(argv[1]) == L"install";
    // Validate the package and unique section before detaching anything.
    if (install) {
        wchar_t preview[] = L"list";
        wchar_t* preview_args[] = {argv[0], preview, argv[2], argv[3], argv[4]};
        const int checked = wmain(5, preview_args);
        if (checked) return checked;
    }
    HDEVINFO set = SetupDiCreateDeviceInfoList(nullptr, nullptr);
    if (set == INVALID_HANDLE_VALUE) return fail("CreateDeviceInfoList");
    SP_DEVINFO_DATA device{}; device.cbSize = sizeof(device);
    if (!SetupDiOpenDeviceInfoW(set, instance.c_str(), nullptr, 0, &device)) {
        int result = fail("OpenDeviceInfo"); SetupDiDestroyDeviceInfoList(set); return result;
    }
    // Preview uses a global list. A real cross-class install first removes the
    // old binding using documented null-driver installation (package retained).
    // The caller must have an exported backup and restore on any later failure.
    if (install) {
        BOOL reboot = FALSE;
        if (!DiInstallDevice(nullptr, set, &device, nullptr, DIIDFLAG_INSTALLNULLDRIVER, &reboot))
            return fail("Detach old binding");
        std::cout << "[DRIVER] Detached exact Bluetooth instance; rebootRequired=" << reboot << '\n';
        SetupDiDestroyDeviceInfoList(set);
        set = SetupDiCreateDeviceInfoList(nullptr, nullptr);
        device = {}; device.cbSize = sizeof(device);
        if (!SetupDiOpenDeviceInfoW(set, instance.c_str(), nullptr, 0, &device)) return fail("Reopen detached device");
    }
    SP_DEVINFO_DATA* scope = install ? &device : nullptr;
    SP_DEVINSTALL_PARAMS_W params{}; params.cbSize = sizeof(params);
    if (!SetupDiGetDeviceInstallParamsW(set, scope, &params)) return fail("GetDeviceInstallParams");
    params.Flags |= DI_ENUMSINGLEINF | DI_QUIETINSTALL;
    params.FlagsEx |= DI_FLAGSEX_ALLOWEXCLUDEDDRVS;
    wcscpy_s(params.DriverPath, inf.c_str());
    if (!SetupDiSetDeviceInstallParamsW(set, scope, &params) ||
        !SetupDiBuildDriverInfoList(set, scope, SPDIT_CLASSDRIVER)) return fail("BuildDriverInfoList");
    SP_DRVINFO_DATA_W selected{}; unsigned matches = 0;
    for (DWORD index = 0;; ++index) {
        SP_DRVINFO_DATA_W driver{}; driver.cbSize = sizeof(driver);
        if (!SetupDiEnumDriverInfoW(set, scope, SPDIT_CLASSDRIVER, index, &driver)) {
            if (GetLastError() != ERROR_NO_MORE_ITEMS) return fail("EnumDriverInfo");
            break;
        }
        std::vector<BYTE> bytes(sizeof(SP_DRVINFO_DETAIL_DATA_W) + 8192, 0);
        auto detail = reinterpret_cast<SP_DRVINFO_DETAIL_DATA_W*>(bytes.data());
        detail->cbSize = sizeof(*detail);
        if (!SetupDiGetDriverInfoDetailW(set, scope, &driver, detail,
                static_cast<DWORD>(bytes.size()), nullptr)) return fail("GetDriverInfoDetail");
        std::wcout << L"[DRIVER] " << driver.Description << L" provider=" << driver.ProviderName
                   << L" section=" << detail->SectionName << L" INF=" << detail->InfFileName << L'\n';
        if (_wcsicmp(section.c_str(), detail->SectionName) == 0) { selected = driver; ++matches; }
    }
    if (matches != 1) {
        std::cerr << "[DRIVER] Expected one selected section; found " << matches << '\n';
        SetupDiDestroyDeviceInfoList(set); return 2;
    }
    std::wcout << L"[DRIVER] Target=" << instance << L" selected=" << selected.Description << L'\n';
    if (!install) { SetupDiDestroyDeviceInfoList(set); return 0; }
    if (!SetupDiSetSelectedDriverW(set, &device, &selected)) return fail("SetSelectedDriver");
    BOOL reboot = FALSE;
    const BOOL ok = DiInstallDevice(nullptr, set, &device, &selected, 0, &reboot);
    const int result = ok ? (reboot ? 3010 : 0) : fail("DiInstallDevice");
    if (ok) std::cout << "[DRIVER] Installed on exact instance; rebootRequired=" << reboot << '\n';
    SetupDiDestroyDeviceInfoList(set);
    return result;
}
