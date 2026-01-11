#pragma once

#define AILO_VK_VALIDATION    0x00000001

#ifndef NDEBUG
#define AILO_VK_FLAGS (AILO_VK_VALIDATION)
#else
#define AILO_VK_FLAGS 0
#endif

#define AILO_VK_ENABLED(flags) (((AILO_VK_FLAGS) & (flags)) == (flags))