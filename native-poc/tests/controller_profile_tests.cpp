#include "controller_profiles.h"
#include <iostream>
int main() {
    if (!hfp_profile_for_instance(L"USB\\VID_8087&PID_0026\\REFERENCE")) return 1;
    if (!hfp_profile_for_instance(L"usb\\vid_8087&pid_0026\\reference")) return 2;
    for (auto id : {L"USB\\VID_8087&PID_00260\\X", L"USB\\VID_8087&PID_0026&MI_00\\X",
                   L"USB\\VID_8087&PID_0026\\", L"USB\\VID_0BDA&PID_8771\\X",
                   L"USB\\VID_8087&PID_0026\\X\\Y", L"PCI\\VID_8087&PID_0026\\X"}) {
        if (hfp_profile_for_instance(id)) return 3;
    }
    const char * path = "\\\\?\\usb#vid_8087&pid_0026#reference#{guid}";
    if (!hfp_usb_path_allowed(path, "USB\\VID_8087&PID_0026\\REFERENCE")) return 4;
    if (hfp_usb_path_allowed(path, "USB\\VID_8087&PID_0026\\REF")) return 5;
    if (hfp_usb_path_allowed(path, "USB\\VID_8087&PID_0026\\OTHER")) return 6;
    if (hfp_usb_path_allowed("\\\\?\\usb#vid_8087&pid_0026&mi_00#reference#{guid}", nullptr)) return 7;
    if (hfp_usb_path_allowed("junk#usb#vid_8087&pid_0026#reference#{guid}", nullptr)) return 8;
    if (hfp_profile_for_usb(0x0bda, 0x8771)) return 9;
    auto p = hfp_profile_for_usb(0x8087, 0x0026);
    if (!p || p->backend != HFP_BACKEND_INTEL_LEGACY) return 10;
    std::cout << "Controller identity, backend and exact-instance isolation passed\n";
}
