#pragma once
// Minimal test adapter for the public InkView calls used by this project.
// This is original test code, not a copy of PocketBook SDK headers.
struct ifont { int size; };
struct imenu { short type; short index; char* text; imenu* submenu; };
struct ibitmap { unsigned short width, height, depth, scanline; unsigned char data[1]; };
using iv_handler = int (*)(int, int, int);
using iv_menuhandler = void (*)(int);
using iv_dialoghandler = void (*)(int);
using iv_keyboardhandler = void (*)(char*);
using iv_timerproc = void (*)();
#define EVT_INIT 21
#define EVT_EXIT 22
#define EVT_SHOW 23
#define EVT_HIDE 24
#define EVT_KEYPRESS 25
#define EVT_KEYREPEAT 28
#define EVT_POINTERUP 29
#define EVT_ORIENTATION 32
#define EVT_BACKGROUND 152
#define EVT_FOREGROUND 151
#define IV_KEY_LEFT 0x13
#define IV_KEY_RIGHT 0x14
#define IV_KEY_OK 0x0a
#define IV_KEY_MENU 0x17
#define IV_KEY_PREV 0x18
#define IV_KEY_NEXT 0x19
#define IV_KEY_BACK 0x1b
#define IV_KEY_PREV2 0x1c
#define IV_KEY_NEXT2 0x1d
#define ITEM_HEADER 1
#define ITEM_ACTIVE 2
#define ALIGN_LEFT 1
#define ALIGN_CENTER 2
#define VALIGN_MIDDLE 32
#define ICON_INFORMATION 1
#define ICON_ERROR 4
#define KBD_NUMERIC 4
void InkViewMain(iv_handler);
void CloseApp();
void SetPanelType(int);
int ScreenWidth();
int ScreenHeight();
int GetOrientation();
void SetOrientation(int);
void ClearScreen();
void DrawLine(int, int, int, int, int);
void DrawRect(int, int, int, int, int);
void FillArea(int, int, int, int, int);
void DrawCircle(int, int, int, int);
ifont* OpenFont(const char*, int, int);
void CloseFont(ifont*);
void SetFont(const ifont*, int);
char* DrawTextRect(int, int, int, int, const char*, int);
void FullUpdate();
void PartialUpdate(int, int, int, int);
void OpenMenu(imenu*, int, int, int, iv_menuhandler);
void Message(int, const char*, const char*, int);
void Dialog(int, const char*, const char*, const char*, const char*, iv_dialoghandler);
void OpenKeyboard(const char*, char*, int, int, iv_keyboardhandler);
void SetHardTimer(const char*, iv_timerproc, int);
void ClearTimer(iv_timerproc);
void ForcingSleep();
ibitmap* BitmapFromScreen(int, int, int, int);
int SaveBitmap(const char*, const ibitmap*);
