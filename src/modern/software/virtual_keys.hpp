#pragma once

// Win32 virtual key codes used by the touch input shim.
//
// The repository's compatibility headers only carry the DirectInput scan codes
// the game reads, so the GetKeyboardState path needs its own constants. These
// are the standard Win32 values, which is what the reconstructed sources expect.

#define TH08_VK_LEFT 0x25
#define TH08_VK_UP 0x26
#define TH08_VK_RIGHT 0x27
#define TH08_VK_DOWN 0x28
#define TH08_VK_RETURN 0x0d
#define TH08_VK_ESCAPE 0x1b
#define TH08_VK_SHIFT 0x10
#define TH08_VK_CONTROL 0x11
#define TH08_VK_NUMPAD2 0x62
#define TH08_VK_NUMPAD4 0x64
#define TH08_VK_NUMPAD6 0x66
#define TH08_VK_NUMPAD8 0x68
