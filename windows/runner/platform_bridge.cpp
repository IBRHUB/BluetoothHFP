#include "platform_bridge.h"
#include "utils.h"
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
#include <wrl/client.h>
#include <shellapi.h>
using Microsoft::WRL::ComPtr;
static HANDLE operation = nullptr;
std::wstring AppDirectory() {
    wchar_t path[32768];
    DWORD count = GetModuleFileNameW(nullptr, path, 32768);
    std::wstring value(path, count);
    return value.substr(0, value.find_last_of(L'\\'));
}
flutter::EncodableList AudioEndpoints() {
    flutter::EncodableList result;
    ComPtr<IMMDeviceEnumerator> enumerator;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator)))) return result;
    for (const auto flow : {eCapture, eRender}) {
        ComPtr<IMMDeviceCollection> devices;
        if (FAILED(enumerator->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, &devices))) continue;
        UINT count = 0; devices->GetCount(&count);
        for (UINT i = 0; i < count; ++i) {
            ComPtr<IMMDevice> device; if (FAILED(devices->Item(i, &device))) continue;
            LPWSTR id = nullptr; if (FAILED(device->GetId(&id))) continue;
            ComPtr<IPropertyStore> props;
            PROPVARIANT name; PropVariantInit(&name);
            if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &props))) props->GetValue(PKEY_Device_FriendlyName, &name);
            using V = flutter::EncodableValue;
            result.emplace_back(flutter::EncodableMap{
                {V("id"), V(Utf8FromUtf16(id))},
                {V("name"), V(name.vt == VT_LPWSTR ? Utf8FromUtf16(name.pwszVal) : "Audio device")},
                {V("capture"), V(flow == eCapture)}});
            CoTaskMemFree(id); PropVariantClear(&name);
        }
    }
    return result;
}
bool BeginDriverOperation(const std::string& action, HWND owner) {
    if (action != "Native" && action != "Headset" && action != "Bootstrap") return false;
    if (operation) return false;
    const auto script = AppDirectory() + L"\\engine\\tools\\Controller.ps1";
    std::wstring params = L"-NoProfile -NonInteractive -ExecutionPolicy Bypass -File \"" + script + L"\" -Action " + std::wstring(action.begin(), action.end());
    wchar_t system[MAX_PATH]; GetSystemDirectoryW(system, MAX_PATH);
    const std::wstring powershell = std::wstring(system) + L"\\WindowsPowerShell\\v1.0\\powershell.exe";
    SHELLEXECUTEINFOW info{}; info.cbSize = sizeof(info); info.fMask = SEE_MASK_NOCLOSEPROCESS;
    info.hwnd = owner; info.lpVerb = L"runas"; info.lpFile = powershell.c_str(); info.lpParameters = params.c_str(); info.nShow = SW_HIDE;
    if (!ShellExecuteExW(&info)) return false;
    operation = info.hProcess; return true;
}
int DriverOperationStatus() {
    if (!operation) return -2;
    if (WaitForSingleObject(operation, 0) != WAIT_OBJECT_0) return -1;
    DWORD code = 1; GetExitCodeProcess(operation, &code); CloseHandle(operation); operation = nullptr;
    return static_cast<int>(code);
}
