#pragma comment(lib, "mincore")

#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00 // _WIN32_WINNT_WIN10
#include <SDKDDKVer.h>

#undef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#undef NOMINMAX
#define NOMINMAX
#include <windows.h>
