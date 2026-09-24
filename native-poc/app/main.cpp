#include "bluetooth/ax201_transport.h"
#include <iostream>
#include <string>

int wmain(int argc, wchar_t** argv) {
    if (argc != 2 || (std::wstring(argv[1]) != L"--probe" && std::wstring(argv[1]) != L"--hci")) {
        std::cout << "Usage: ax201_probe --probe | --hci\n"
                     "Stage 1 only: inventory, USB descriptors, WinUSB open.\n"
                     "No driver changes, HCI commands, firmware writes or audio capture.\n"
                     "Exit 0=WinUSB and HCI endpoint layout verified, 2=absent,\n"
                     "     3=ownership/open blocked, 4=descriptor/endpoint failure.\n";
        return argc == 1 ? 0 : 64;
    }
    return ax201::probe(std::wstring(argv[1]) == L"--hci");
}
