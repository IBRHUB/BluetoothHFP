#include "control.h"
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <mutex>
static std::mutex output_lock;
static bool enabled;
static std::string pending;
static std::string escape(const char* s) {
    std::string out;
    for (; *s; ++s) {
        const unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') { out += '\\'; out += (char)c; }
        else if (c >= 32) out += (char)c;
    }
    return out;
}
extern "C" void control_init(void) { enabled = GetEnvironmentVariableA("AX201_IPC", nullptr, 0) != 0; }
extern "C" void control_event(const char* event, const char* value, int status) {
    if (!enabled) return;
    std::lock_guard<std::mutex> lock(output_lock);
    printf("@{\"version\":1,\"event\":\"%s\",\"value\":\"%s\",\"status\":%d}\n",
        escape(event).c_str(), escape(value ? value : "").c_str(), status);
}
extern "C" int control_read(char* command, unsigned capacity) {
    if (!enabled) return 0;
    HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    DWORD available = 0;
    if (!PeekNamedPipe(input, nullptr, 0, nullptr, &available, nullptr)) return -1;
    if (available) {
        char chunk[512]; DWORD read = 0;
        if (!ReadFile(input, chunk, available > sizeof(chunk) ? sizeof(chunk) : available, &read, nullptr)) return -1;
        pending.append(chunk, read);
    }
    auto end = pending.find('\n');
    if (end == std::string::npos) { if (pending.size() > 1024) return -1; return 0; }
    const auto line = pending.substr(0, end);
    pending.erase(0, end + 1);
    if (line.size() >= capacity) { control_event("error", "Command too long", 64); return 0; }
    strcpy_s(command, capacity, line.c_str());
    command[strcspn(command, "\r\n")] = 0;
    return 1;
}
