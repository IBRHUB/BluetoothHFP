#pragma once
// Windows SDK 19041 predates its C11 stdalign.h. MSVC /std:c11 implements
// the language operators; supply the standard convenience macros for LC3.
#define alignas _Alignas
#define alignof _Alignof
#define __alignas_is_defined 1
#define __alignof_is_defined 1
