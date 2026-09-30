// Minimal Win32 stub so src/Decoder.cpp can be compiled and self-tested on Linux.
#pragma once
#include <cstddef>
#include <cstring>
#include <cwchar>
typedef unsigned char BYTE; typedef unsigned short WORD; typedef unsigned long DWORD;
typedef int BOOL; typedef long LONG; typedef unsigned int UINT; typedef unsigned long ULONG;
typedef long long LONGLONG; typedef unsigned long long ULONGLONG;
typedef wchar_t WCHAR; typedef char CHAR;
typedef WCHAR* PWSTR; typedef WCHAR* LPWSTR; typedef const WCHAR* LPCWSTR; typedef const WCHAR* PCWSTR;
typedef char* LPSTR; typedef const char* LPCSTR;
typedef void* LPVOID; typedef const void* LPCVOID; typedef void VOID;
typedef long HRESULT; typedef unsigned long COLORREF;
typedef long long INT_PTR; typedef unsigned long long UINT_PTR;
typedef INT_PTR LONG_PTR; typedef UINT_PTR ULONG_PTR;
typedef UINT_PTR WPARAM; typedef LONG_PTR LPARAM; typedef LONG_PTR LRESULT;
#define WINAPI
#define CALLBACK
#define APIENTRY
#define STDAPI extern "C" HRESULT
#define STDAPI_(t) extern "C" t
#define DECLSPEC_NOTHROW
#define MAX_PATH 260
#define TRUE 1
#define FALSE 0
#define S_OK ((HRESULT)0L)
#define S_FALSE ((HRESULT)1L)
#define E_FAIL ((HRESULT)0x80004005L)
#define E_INVALIDARG ((HRESULT)0x80070057L)
#define E_NOTIMPL ((HRESULT)0x80004001L)
#define E_NOINTERFACE ((HRESULT)0x80004002L)
#define E_OUTOFMEMORY ((HRESULT)0x8007000EL)
#define SUCCEEDED(hr) (((HRESULT)(hr)) >= 0)
#define FAILED(hr) (((HRESULT)(hr)) < 0)
#define ARRAYSIZE(a) (sizeof(a)/sizeof((a)[0]))
#define LOWORD(l) ((WORD)(((ULONG_PTR)(l)) & 0xffff))
#define HIWORD(l) ((WORD)((((ULONG_PTR)(l)) >> 16) & 0xffff))
#define RGB(r,g,b) ((COLORREF)(((BYTE)(r)|((WORD)((BYTE)(g))<<8))|(((DWORD)(BYTE)(b))<<16)))
#define GetRValue(c) ((BYTE)(c))
#define GetGValue(c) ((BYTE)(((WORD)(c))>>8))
#define GetBValue(c) ((BYTE)((c)>>16))
#define MAKEINTRESOURCEW(i) ((LPWSTR)((ULONG_PTR)((WORD)(i))))
#define INVALID_HANDLE_VALUE ((HANDLE)(LONG_PTR)-1)
#define ERROR_SUCCESS 0L
#define ERROR_ALREADY_EXISTS 183L
#define ERROR_FILE_NOT_FOUND 2L
#define ERROR_FILE_INVALID 1006L
struct HANDLE__; typedef void* HANDLE;
typedef HANDLE HWND; typedef HANDLE HINSTANCE; typedef HINSTANCE HMODULE;
typedef HANDLE HMENU; typedef HANDLE HDC; typedef HANDLE HGDIOBJ;
typedef HANDLE HFONT; typedef HANDLE HBRUSH; typedef HANDLE HPEN; typedef HANDLE HBITMAP;
typedef HANDLE HICON; typedef HICON HCURSOR; typedef HANDLE HKEY; typedef HANDLE HRGN;
typedef HANDLE HACCEL; typedef HANDLE HGLOBAL;
struct POINT { LONG x, y; };
struct SIZE { LONG cx, cy; };
struct RECT { LONG left, top, right, bottom; };
typedef RECT* LPRECT; typedef const RECT* LPCRECT;
struct FILETIME { DWORD dwLowDateTime, dwHighDateTime; };
struct MSG { HWND hwnd; UINT message; WPARAM wParam; LPARAM lParam; DWORD time; POINT pt; };
struct WIN32_FILE_ATTRIBUTE_DATA { DWORD dwFileAttributes; FILETIME ftCreationTime,
  ftLastAccessTime, ftLastWriteTime; DWORD nFileSizeHigh, nFileSizeLow; };
enum GET_FILEEX_INFO_LEVELS { GetFileExInfoStandard = 0 };
typedef LRESULT (CALLBACK* WNDPROC)(HWND, UINT, WPARAM, LPARAM);
struct WNDCLASSEXW { UINT cbSize; UINT style; WNDPROC lpfnWndProc; int cbClsExtra;
  int cbWndExtra; HINSTANCE hInstance; HICON hIcon; HCURSOR hCursor; HBRUSH hbrBackground;
  LPCWSTR lpszMenuName; LPCWSTR lpszClassName; HICON hIconSm; };
struct DRAWITEMSTRUCT { UINT CtlType; UINT CtlID; UINT itemID; UINT itemAction;
  UINT itemState; HWND hwndItem; HDC hDC; RECT rcItem; ULONG_PTR itemData; };
#define LF_FACESIZE 32
struct LOGFONTW { LONG lfHeight, lfWidth, lfEscapement, lfOrientation, lfWeight;
  BYTE lfItalic, lfUnderline, lfStrikeOut, lfCharSet, lfOutPrecision,
  lfClipPrecision, lfQuality, lfPitchAndFamily; WCHAR lfFaceName[LF_FACESIZE]; };
typedef LOGFONTW* LPLOGFONTW;
struct SECURITY_ATTRIBUTES { DWORD nLength; LPVOID lpSecurityDescriptor; BOOL bInheritHandle; };
typedef SECURITY_ATTRIBUTES* LPSECURITY_ATTRIBUTES;
typedef struct _OVERLAPPED* LPOVERLAPPED;
// ---- window styles / messages ----
#define WS_CHILD 0x40000000L
#define WS_VISIBLE 0x10000000L
#define WS_BORDER 0x00800000L
#define WS_GROUP 0x00020000L
#define WS_TABSTOP 0x00010000L
#define WS_OVERLAPPED 0x00000000L
#define WS_CAPTION 0x00C00000L
#define WS_SYSMENU 0x00080000L
#define SS_LEFT 0x0000L
#define SS_CENTERIMAGE 0x0200L
#define SS_OWNERDRAW 0x000DL
#define BS_PUSHBUTTON 0x0000L
#define BS_DEFPUSHBUTTON 0x0001L
#define BS_AUTOCHECKBOX 0x0003L
#define BS_AUTORADIOBUTTON 0x0009L
#define BS_OWNERDRAW 0x000BL
#define ES_LEFT 0x0000L
#define ES_NUMBER 0x2000L
#define BST_CHECKED 1
#define BST_UNCHECKED 0
#define CW_USEDEFAULT ((int)0x80000000)
#define SW_SHOWNORMAL 1
#define SW_RESTORE 9
#define WM_CREATE 0x0001
#define WM_DESTROY 0x0002
#define WM_CLOSE 0x0010
#define WM_SETFONT 0x0030
#define WM_COMMAND 0x0111
#define WM_DRAWITEM 0x002B
#define EN_CHANGE 0x0300
#define IDOK 1
#define IDCANCEL 2
#define ODS_FOCUS 0x0010
#define ODT_STATIC 5
#define ODT_BUTTON 4
#define GWLP_HINSTANCE (-6)
#define GWLP_USERDATA (-21)
#define IDC_ARROW MAKEINTRESOURCEW(32512)
#define MB_OK 0x0L
#define MB_ICONWARNING 0x30L
#define MB_ICONINFORMATION 0x40L
#define COLOR_WINDOW 5
#define COLOR_WINDOWTEXT 8
#define COLOR_GRAYTEXT 17
#define COLOR_BTNFACE 15
#define DT_LEFT 0x0
#define DT_CENTER 0x1
#define DT_VCENTER 0x4
#define DT_SINGLELINE 0x20
#define DT_NOCLIP 0x100
#define TRANSPARENT 1
#define OPAQUE 2
#define FW_NORMAL 400
#define FW_SEMIBOLD 600
#define DEFAULT_CHARSET 1
#define OUT_DEFAULT_PRECIS 0
#define CLIP_DEFAULT_PRECIS 0
#define ANTIALIASED_QUALITY 4
#define CLEARTYPE_QUALITY 5
#define DEFAULT_PITCH 0
#define FF_DONTCARE 0
#define BLACK_BRUSH 4
#define GRAY_BRUSH 2
#define WHITE_BRUSH 0
#define LOGPIXELSX 88
#define LOGPIXELSY 90
#define SM_CXSMICON 49
#define BI_RGB 0
#define DIB_RGB_COLORS 0
#define GENERIC_READ 0x80000000L
#define GENERIC_WRITE 0x40000000L
#define FILE_SHARE_READ 0x00000001L
#define FILE_SHARE_WRITE 0x00000002L
#define OPEN_EXISTING 3
#define CREATE_NEW 1
#define CREATE_ALWAYS 2
#define FILE_ATTRIBUTE_NORMAL 0x80
#define CP_UTF8 65001
#define CP_ACP 0
#define MB_ERR_INVALID_CHARS 0x8
#define HKEY_CURRENT_USER ((HKEY)(ULONG_PTR)0x80000001)
#define HKEY_LOCAL_MACHINE ((HKEY)(ULONG_PTR)0x80000002)
#define KEY_QUERY_VALUE 0x0001
#define REG_DWORD 4
// ---- user32 ----
HWND CreateWindowExW(DWORD, LPCWSTR, LPCWSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, LPVOID);
LRESULT SendMessageW(HWND, UINT, WPARAM, LPARAM);
LRESULT DefWindowProcW(HWND, UINT, WPARAM, LPARAM);
BOOL SetDlgItemTextW(HWND, int, LPCWSTR);
UINT GetDlgItemTextW(HWND, int, LPWSTR, int);
HWND GetDlgItem(HWND, int);
BOOL CheckDlgButton(HWND, int, UINT);
UINT IsDlgButtonChecked(HWND, int);
BOOL EnableWindow(HWND, BOOL);
BOOL InvalidateRect(HWND, LPCRECT, BOOL);
WORD RegisterClassExW(const WNDCLASSEXW*);
BOOL ShowWindow(HWND, int);
BOOL UpdateWindow(HWND);
BOOL GetMessageW(MSG*, HWND, UINT, UINT);
BOOL IsDialogMessageW(HWND, MSG*);
BOOL TranslateMessage(const MSG*);
LRESULT DispatchMessageW(const MSG*);
void PostQuitMessage(int);
BOOL DestroyWindow(HWND);
int MessageBoxW(HWND, LPCWSTR, LPCWSTR, UINT);
LONG_PTR GetWindowLongPtrW(HWND, int);
LONG_PTR SetWindowLongPtrW(HWND, int, LONG_PTR);
int MulDiv(int, int, int);
DWORD GetSysColor(int);
HCURSOR LoadCursorW(HINSTANCE, LPCWSTR);
HICON LoadIconW(HINSTANCE, LPCWSTR);
BOOL AdjustWindowRect(LPRECT, DWORD, BOOL);
HWND FindWindowW(LPCWSTR, LPCWSTR);
BOOL SetForegroundWindow(HWND);
BOOL SetProcessDPIAware(void);
HDC GetDC(HWND);
int ReleaseDC(HWND, HDC);
int GetSystemMetrics(int);
BOOL DrawFocusRect(HDC, const RECT*);
int FillRect(HDC, const RECT*, HBRUSH);
int FrameRect(HDC, const RECT*, HBRUSH);
int DrawTextW(HDC, LPCWSTR, int, LPRECT, UINT);
// ---- gdi32 ----
HBRUSH CreateSolidBrush(COLORREF);
HGDIOBJ GetStockObject(int);
HFONT CreateFontW(int,int,int,int,int,DWORD,DWORD,DWORD,DWORD,DWORD,DWORD,DWORD,DWORD,LPCWSTR);
HGDIOBJ SelectObject(HDC, HGDIOBJ);
BOOL DeleteObject(HGDIOBJ);
HDC CreateCompatibleDC(HDC);
BOOL DeleteDC(HDC);
int SetBkMode(HDC, int);
COLORREF SetTextColor(HDC, COLORREF);
int GetDeviceCaps(HDC, int);
BOOL GetTextExtentPoint32W(HDC, LPCWSTR, int, SIZE*);
struct BITMAPINFOHEADER { DWORD biSize; LONG biWidth, biHeight; WORD biPlanes, biBitCount;
  DWORD biCompression, biSizeImage; LONG biXPelsPerMeter, biYPelsPerMeter; DWORD biClrUsed, biClrImportant; };
struct RGBQUAD { BYTE rgbBlue, rgbGreen, rgbRed, rgbReserved; };
struct BITMAPINFO { BITMAPINFOHEADER bmiHeader; RGBQUAD bmiColors[1]; };
struct ICONINFO { BOOL fIcon; DWORD xHotspot, yHotspot; HBITMAP hbmMask, hbmColor; };
HBITMAP CreateDIBSection(HDC, const BITMAPINFO*, UINT, void**, HANDLE, DWORD);
HBITMAP CreateBitmap(int, int, UINT, UINT, const void*);
HICON CreateIconIndirect(ICONINFO*);
// ---- kernel32 ----
HANDLE CreateMutexW(LPSECURITY_ATTRIBUTES, BOOL, LPCWSTR);
DWORD GetLastError(void);
BOOL CloseHandle(HANDLE);
DWORD GetTickCount(void);
HANDLE CreateFileW(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
BOOL WriteFile(HANDLE, LPCVOID, DWORD, DWORD*, LPOVERLAPPED);
BOOL ReadFile(HANDLE, LPVOID, DWORD, DWORD*, LPOVERLAPPED);
DWORD GetFileSize(HANDLE, DWORD*);
BOOL GetFileAttributesExW(LPCWSTR, GET_FILEEX_INFO_LEVELS, LPVOID);
LONG CompareFileTime(const FILETIME*, const FILETIME*);
int MultiByteToWideChar(UINT, DWORD, LPCSTR, int, LPWSTR, int);
int WideCharToMultiByte(UINT, DWORD, LPCWSTR, int, LPSTR, int, LPCSTR, BOOL*);
DWORD GetModuleFileNameW(HMODULE, LPWSTR, DWORD);
LPWSTR lstrcpynW(LPWSTR, LPCWSTR, int);
int lstrlenW(LPCWSTR);
LONG RegOpenKeyExW(HKEY, LPCWSTR, DWORD, DWORD, HKEY*);
LONG RegQueryValueExW(HKEY, LPCWSTR, DWORD*, DWORD*, BYTE*, DWORD*);
LONG RegCloseKey(HKEY);
union LARGE_INTEGER { struct { DWORD LowPart; LONG HighPart; }; LONGLONG QuadPart; };
BOOL GetFileSizeEx(HANDLE, LARGE_INTEGER*);
