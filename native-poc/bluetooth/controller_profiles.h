#pragma once
#include <stddef.h>
#include <string.h>
#include <wchar.h>

enum { HFP_BACKEND_INTEL_LEGACY = 1 };
typedef struct {
    unsigned vid, pid, backend;
    const char * id;
    const wchar_t * instance_prefix;
    const char * path_prefix;
} hfp_controller_profile;

static const hfp_controller_profile hfp_controller_profiles[] = {
#include "controller_profile_entries.inc"
};

static inline const hfp_controller_profile * hfp_profile_for_usb(unsigned vid, unsigned pid) {
    size_t i;
    for (i = 0; i < sizeof(hfp_controller_profiles) / sizeof(hfp_controller_profiles[0]); ++i) {
        const hfp_controller_profile * p = &hfp_controller_profiles[i];
        if (p->vid == vid && p->pid == pid) return p;
    }
    return NULL;
}

static inline const hfp_controller_profile * hfp_profile_for_instance(const wchar_t * instance) {
    size_t i;
    for (i = 0; i < sizeof(hfp_controller_profiles) / sizeof(hfp_controller_profiles[0]); ++i) {
        const hfp_controller_profile * p = &hfp_controller_profiles[i];
        const size_t n = wcslen(p->instance_prefix);
        if (_wcsnicmp(instance, p->instance_prefix, n) == 0 && instance[n] && !wcschr(instance + n, L'\\')) return p;
    }
    return NULL;
}

static inline int hfp_usb_path_allowed(const char * path, const char * selected) {
    size_t i;
    if (strncmp(path, "\\\\?\\", 4) != 0) return 0;
    for (i = 0; i < sizeof(hfp_controller_profiles) / sizeof(hfp_controller_profiles[0]); ++i) {
        const hfp_controller_profile * p = &hfp_controller_profiles[i];
        const size_t n = strlen(p->path_prefix);
        if (_strnicmp(path + 4, p->path_prefix, n) != 0) continue;
        if (!path[4 + n] || path[4 + n] == '#') return 0;
        if (selected && selected[0]) {
            char match[512];
            size_t j, length = strlen(selected);
            if (length + 2 > sizeof(match)) return 0;
            for (j = 0; j < length; ++j) match[j] = selected[j] == '\\' ? '#' : selected[j];
            match[length] = '#'; match[length + 1] = 0;
            return _strnicmp(path + 4, match, length + 1) == 0;
        }
        return 1;
    }
    return 0;
}
