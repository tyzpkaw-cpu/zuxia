#include "Globals.h"

HINSTANCE g_hInst = nullptr;
LONG g_cRefDll = -1;  // Microsoft TSF sample convention: -1 means no refs.
CRITICAL_SECTION g_cs;

// {A0073A11-FF52-4185-A655-D0C9171B7850}
const CLSID c_clsidTextService = {
    0xa0073a11,
    0xff52,
    0x4185,
    {0xa6, 0x55, 0xd0, 0xc9, 0x17, 0x1b, 0x78, 0x50}};

// {699B0EC1-3FDB-415D-89C0-0E55AA2EFFAF}
const GUID c_guidProfile = {
    0x699b0ec1,
    0x3fdb,
    0x415d,
    {0x89, 0xc0, 0x0e, 0x55, 0xaa, 0x2e, 0xff, 0xaf}};

// {7B73DF85-7342-4B2C-9A90-41482526B47B}
// Preserved key for the Shift tap that switches Chinese/Western input.
const GUID c_guidToggleAsciiKey = {
    0x7b73df85,
    0x7342,
    0x4b2c,
    {0x9a, 0x90, 0x41, 0x48, 0x25, 0x26, 0xb4, 0x7b}};
