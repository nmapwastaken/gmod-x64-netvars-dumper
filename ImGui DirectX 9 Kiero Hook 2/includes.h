#pragma once
#define _CRT_SECURE_NO_WARNINGS
#include <Windows.h>
#include <d3d9.h>
#include <d3dx9.h>
#include "kiero/kiero.h"
#include "kiero/minhook/include/MinHook.h"
#include "imgui/imgui.h"
#include "imgui/imgui_impl_win32.h"
#include "imgui/imgui_impl_dx9.h"
#include <string>
#include <vector>
#include <fstream>
#include <iostream>
#include <ctime>
#define WINDOW_NAME "Dear ImGui DirectX9 Example"
typedef long(__stdcall* EndScene)(LPDIRECT3DDEVICE9);
typedef LRESULT(CALLBACK* WNDPROC)(HWND, UINT, WPARAM, LPARAM);

void InitializeGModSDK();

#define LOG_SDK(msg) { \
    time_t now = time(0); \
    tm* ltm = localtime(&now); \
    const char* file = strrchr(__FILE__, '\\'); \
    file = file ? file + 1 : __FILE__; \
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE); \
    SetConsoleTextAttribute(hConsole, 10); /* green */ \
    printf("[%02d:%02d:%02d] ", ltm->tm_hour, ltm->tm_min, ltm->tm_sec); \
    SetConsoleTextAttribute(hConsole, 8); /* gray */ \
    printf("[%s:%d]: ", file, __LINE__); \
    SetConsoleTextAttribute(hConsole, 7); /* white (default) */ \
    printf("%s\n", msg); \
}