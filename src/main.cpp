#define NOMINMAX
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <dwmapi.h>
#include <mmsystem.h>
#include <commdlg.h>
#include <shellapi.h> // NEW: For Drag and Drop
#include <string>
#include <vector>
#include <algorithm>
#include "quantis_core.h"

using namespace Gdiplus;

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#define DWMWCP_ROUND 2
#endif

// --- STATE VARIABLES ---
bool isPaused = false;
bool hasLoadedFile = false;
int songLengthMs = 0;
int songPosMs = 0;
bool isDraggingSlider = false;
std::string currentFileName = "No File Loaded";

RECT rectPlay; RECT rectEncode; RECT rectDecode; RECT rectSlider;
POINT mousePos = {0, 0};

// --- CUSTOM MENU SYSTEM ---
struct DropdownItem { std::wstring name; int id; };
struct MenuCategory { std::wstring name; RECT rect; std::vector<DropdownItem> items; };

std::vector<MenuCategory> menus = {
    { L"File", {0, 0, 60, 30}, { {L"Open Quantis File...", 1}, {L"Unload Track", 2}, {L"Exit Player", 3} } },
    { L"Playback", {60, 0, 150, 30}, { {L"Play / Pause", 4}, {L"Stop Audio", 5}, {L"Skip +10s", 6}, {L"Rewind -10s", 7} } },
    { L"Tools", {150, 0, 220, 30}, { {L"Encode (WAV \x2192 QNT)", 8}, {L"Decode (QNT \x2192 WAV)", 9} } },
    { L"Help", {220, 0, 280, 30}, { {L"About Quantis...", 10} } }
};
int openMenuIndex = -1;

// --- UTILS ---
std::wstring formatTime(int ms) {
    int totalSec = ms / 1000;
    int m = totalSec / 60;
    int s = totalSec % 60;
    wchar_t buf[32];
    swprintf(buf, 32, L"%02d:%02d", m, s);
    return std::wstring(buf);
}

std::string openFileDialog(const char* filter, HWND owner) {
    OPENFILENAMEA ofn; char szFile[260] = {0}; ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = owner; ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile); ofn.lpstrFilter = filter; ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameA(&ofn)) return szFile; return "";
}

std::string getTempPlayPath(const std::string& originalFile) {
    char tempPath[MAX_PATH]; GetTempPathA(MAX_PATH, tempPath); 
    std::string qTemp = std::string(tempPath) + "Quantis\\";
    CreateDirectoryA(qTemp.c_str(), NULL);
    size_t slashPos = originalFile.find_last_of("/\\");
    std::string fName = (slashPos == std::string::npos) ? originalFile : originalFile.substr(slashPos + 1);
    return qTemp + fName + "_temp.wav";
}

std::string getOutputPath(const std::string& inputPath, const std::string& newExt) {
    size_t dotPos = inputPath.find_last_of('.');
    if (dotPos == std::string::npos) return inputPath + newExt;
    return inputPath.substr(0, dotPos) + newExt;
}

void AddRoundRect(GraphicsPath& path, int x, int y, int width, int height, int radius) {
    path.AddArc(x, y, radius * 2, radius * 2, 180, 90);
    path.AddArc(x + width - radius * 2, y, radius * 2, radius * 2, 270, 90);
    path.AddArc(x + width - radius * 2, y + height - radius * 2, radius * 2, radius * 2, 0, 90);
    path.AddArc(x, y + height - radius * 2, radius * 2, radius * 2, 90, 90);
    path.CloseFigure();
}

void playFile(HWND hwnd, const std::string& file) {
    std::string tempFile = getTempPlayPath(file);
    decodeQntToWav(file, tempFile);
    mciSendStringA("close qplayer", NULL, 0, NULL);
    std::string openCmd = "open \"" + tempFile + "\" type waveaudio alias qplayer";
    mciSendStringA(openCmd.c_str(), NULL, 0, NULL);
    mciSendStringA("set qplayer time format milliseconds", NULL, 0, NULL);
    
    char szLen[64];
    mciSendStringA("status qplayer length", szLen, 64, NULL);
    songLengthMs = (strlen(szLen) > 0) ? std::stoi(szLen) : 0;
    
    mciSendStringA("play qplayer", NULL, 0, NULL);
    isPaused = false;
    hasLoadedFile = true;
    
    size_t slashPos = file.find_last_of("/\\");
    currentFileName = (slashPos == std::string::npos) ? file : file.substr(slashPos + 1);
}

void HandleMenuAction(int id, HWND hwnd) {
    if (id == 1) {
        std::string f = openFileDialog("Quantis Audio\0*.qnt\0", hwnd);
        if (!f.empty()) playFile(hwnd, f);
    } 
    else if (id == 2) {
        mciSendStringA("close qplayer", NULL, 0, NULL);
        isPaused = false; hasLoadedFile = false; songPosMs = 0; songLengthMs = 0;
        currentFileName = "No File Loaded";
    } 
    else if (id == 3) { PostQuitMessage(0); }
    else if (id == 4 && hasLoadedFile) {
        if (isPaused) { mciSendStringA("play qplayer", NULL, 0, NULL); isPaused = false; }
        else { mciSendStringA("pause qplayer", NULL, 0, NULL); isPaused = true; }
    }
    else if (id == 5 && hasLoadedFile) {
        mciSendStringA("seek qplayer to 0", NULL, 0, NULL);
        mciSendStringA("pause qplayer", NULL, 0, NULL); isPaused = true;
    }
    else if (id == 6 && hasLoadedFile) { 
        songPosMs = std::min(songLengthMs, songPosMs + 10000);
        std::string cmd = "seek qplayer to " + std::to_string(songPosMs);
        mciSendStringA(cmd.c_str(), NULL, 0, NULL);
        if(!isPaused) mciSendStringA("play qplayer", NULL, 0, NULL);
    }
    else if (id == 7 && hasLoadedFile) { 
        songPosMs = std::max(0, songPosMs - 10000);
        std::string cmd = "seek qplayer to " + std::to_string(songPosMs);
        mciSendStringA(cmd.c_str(), NULL, 0, NULL);
        if(!isPaused) mciSendStringA("play qplayer", NULL, 0, NULL);
    }
    else if (id == 8) {
        std::string f = openFileDialog("WAV Audio\0*.wav\0", hwnd);
        if (!f.empty()) {
            encodeWavToQnt(f, getOutputPath(f, "-converted.qnt"));
            MessageBoxA(hwnd, "WAV successfully encoded to QNT!", "Quantis Encoder", MB_OK | MB_ICONINFORMATION);
        }
    }
    else if (id == 9) {
        std::string f = openFileDialog("Quantis Audio\0*.qnt\0", hwnd);
        if (!f.empty()) {
            decodeQntToWav(f, getOutputPath(f, "-converted.wav"));
            MessageBoxA(hwnd, "QNT successfully decoded to WAV!", "Quantis Decoder", MB_OK | MB_ICONINFORMATION);
        }
    }
    else if (id == 10) {
        MessageBoxA(hwnd, "Quantis Audio Codec v1.3.0\nEngineered by Lucid Audio\n\nCustom 16-bit ADPCM Compression DSP Engine.", "About Quantis", MB_OK | MB_ICONINFORMATION);
    }
}

void UpdateLayout(int w, int h) {
    // NEW: Margins are wider to fit the 00:00 time text on the sides
    rectSlider = { 80, h - 200, w - 80, h - 180 };
    int playSize = 60;
    rectPlay = { (w - playSize) / 2, h - 150, (w + playSize) / 2, h - 90 };
    int btnW = (w - 90) / 2; 
    rectEncode = { 30, h - 70, 30 + btnW, h - 30 };
    rectDecode = { 60 + btnW, h - 70, 60 + btnW * 2, h - 30 };
}

// --- THE GDI+ RENDERING ENGINE (60 FPS) ---
void DrawCustomUI(HWND hwnd, HDC hdc) {
    RECT cr; GetClientRect(hwnd, &cr);
    int w = cr.right; int h = cr.bottom;

    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP memBitmap = CreateCompatibleBitmap(hdc, w, h);
    SelectObject(memDC, memBitmap);

    Graphics g(memDC);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintAntiAlias);

    g.Clear(Color(255, 11, 15, 23)); 

    FontFamily ff(L"Segoe UI");
    Font fontTitle(&ff, 18, FontStyleBold, UnitPixel);
    Font fontNormal(&ff, 14, FontStyleRegular, UnitPixel);
    Font fontSmall(&ff, 12, FontStyleRegular, UnitPixel);
    SolidBrush brushText(Color(255, 240, 240, 240));
    SolidBrush brushTextMuted(Color(255, 130, 140, 160));
    SolidBrush brushAccent(Color(255, 59, 130, 246));
    SolidBrush brushPanel(Color(255, 25, 30, 45));

    StringFormat centerFormat; 
    centerFormat.SetAlignment(StringAlignmentCenter); 
    centerFormat.SetLineAlignment(StringAlignmentCenter);

    g.DrawString(L"QUANTIS AUDIO", -1, &fontTitle, RectF(0, 45, (REAL)w, 30), &centerFormat, &brushText);
    g.DrawString(L"Lucid DSP Engine", -1, &fontSmall, RectF(0, 70, (REAL)w, 20), &centerFormat, &brushAccent);

    int artW = w - 160; 
    int artH = h - 360; 
    if (artW < 100) artW = 100; if (artH < 100) artH = 100; 
    int artX = (w - artW) / 2;
    int artY = 110;
    
    GraphicsPath artPath; AddRoundRect(artPath, artX, artY, artW, artH, 15);
    LinearGradientBrush artBrush(Rect(artX, artY, artW, artH), Color(255, 30, 40, 60), Color(255, 15, 20, 35), LinearGradientModeForwardDiagonal);
    g.FillPath(&artBrush, &artPath);
    
    std::wstring widename(currentFileName.begin(), currentFileName.end());
    g.DrawString(widename.c_str(), -1, &fontNormal, RectF((REAL)artX, (REAL)artY, (REAL)artW, (REAL)artH), &centerFormat, &brushText);

    // --- TIMELINE SLIDER WITH LIVE TIME TEXT ---
    int sliderX = rectSlider.left;
    int sliderWidth = rectSlider.right - rectSlider.left;
    int sliderY = rectSlider.top + (rectSlider.bottom - rectSlider.top) / 2 - 3;
    
    std::wstring curTime = formatTime(songPosMs);
    std::wstring totTime = formatTime(songLengthMs);
    g.DrawString(curTime.c_str(), -1, &fontSmall, RectF(20.0f, (REAL)(sliderY - 10), 50.0f, 20.0f), &centerFormat, &brushTextMuted);
    g.DrawString(totTime.c_str(), -1, &fontSmall, RectF((REAL)(w - 70), (REAL)(sliderY - 10), 50.0f, 20.0f), &centerFormat, &brushTextMuted);
    
    GraphicsPath trackPath; AddRoundRect(trackPath, sliderX, sliderY, sliderWidth, 6, 3);
    g.FillPath(&brushPanel, &trackPath);
    
    float progress = (songLengthMs > 0) ? (float)songPosMs / songLengthMs : 0.0f;
    if (isDraggingSlider) progress = (float)(mousePos.x - sliderX) / sliderWidth;
    if (progress < 0.0f) progress = 0.0f; if (progress > 1.0f) progress = 1.0f;

    if (progress > 0) {
        GraphicsPath fillPath; AddRoundRect(fillPath, sliderX, sliderY, (int)(sliderWidth * progress), 6, 3);
        g.FillPath(&brushAccent, &fillPath);
    }
    
    bool hoverSlider = PtInRect(&rectSlider, mousePos);
    int thumbSize = (hoverSlider || isDraggingSlider) ? 14 : 10;
    int thumbX = sliderX + (int)(sliderWidth * progress) - (thumbSize / 2);
    g.FillEllipse(&brushText, (INT)thumbX, (INT)(sliderY + 3 - (thumbSize / 2)), (INT)thumbSize, (INT)thumbSize);

    // --- QUICK DASHBOARD BUTTONS ---
    auto drawBtn = [&](RECT r, const WCHAR* txt, bool isPrimary, bool isCircle, bool isDisabled = false) {
        bool hovered = PtInRect(&r, mousePos) && !isDisabled && (openMenuIndex == -1); 
        Color bgColor;
        
        if (isDisabled) { bgColor = Color(255, 30, 35, 45); } 
        else if (isPrimary) { bgColor = hovered ? Color(255, 96, 165, 250) : Color(255, 59, 130, 246); } 
        else { bgColor = hovered ? Color(255, 40, 45, 60) : Color(255, 25, 30, 45); }
        SolidBrush bgBrush(bgColor);
        SolidBrush tBrush(isDisabled ? Color(255, 100, 110, 120) : Color(255, 240, 240, 240));

        GraphicsPath btnPath;
        if (isCircle) { g.FillEllipse(&bgBrush, (INT)r.left, (INT)r.top, (INT)(r.right - r.left), (INT)(r.bottom - r.top)); } 
        else { AddRoundRect(btnPath, r.left, r.top, r.right - r.left, r.bottom - r.top, 8); g.FillPath(&bgBrush, &btnPath); }
        g.DrawString(txt, -1, &fontNormal, RectF((REAL)r.left, (REAL)r.top, (REAL)(r.right-r.left), (REAL)(r.bottom-r.top)), &centerFormat, &tBrush);
    };

    drawBtn(rectPlay, isPaused ? L"PLAY" : L"PAUSE", true, true, !hasLoadedFile); 
    drawBtn(rectEncode, L"Encode WAV \x2192 QNT", false, false);
    drawBtn(rectDecode, L"Decode QNT \x2192 WAV", false, false);

    // --- THE CUSTOM GDI+ MENU SYSTEM ---
    SolidBrush topBarBrush(Color(255, 20, 25, 35));
    g.FillRectangle(&topBarBrush, 0, 0, w, 30); 

    for (int i = 0; i < (int)menus.size(); ++i) {
        auto& menu = menus[i];
        bool hovered = PtInRect(&menu.rect, mousePos) || (openMenuIndex == i);
        if (hovered) {
            SolidBrush hBrush(Color(255, 40, 45, 60));
            g.FillRectangle(&hBrush, (INT)menu.rect.left, (INT)menu.rect.top, (INT)(menu.rect.right - menu.rect.left), (INT)(menu.rect.bottom - menu.rect.top));
        }
        g.DrawString(menu.name.c_str(), -1, &fontSmall, RectF((REAL)menu.rect.left, (REAL)menu.rect.top, (REAL)(menu.rect.right-menu.rect.left), (REAL)(menu.rect.bottom-menu.rect.top)), &centerFormat, &brushText);
    }

    if (openMenuIndex != -1) {
        auto& menu = menus[openMenuIndex];
        int dropW = 200; int itemH = 35; int dropH = (int)menu.items.size() * itemH;
        int dropX = menu.rect.left; int dropY = 30;
        
        SolidBrush dropBg(Color(255, 25, 30, 45));
        SolidBrush dropBorder(Color(255, 60, 65, 80));
        
        g.FillRectangle(&dropBg, dropX, dropY, dropW, dropH);
        Pen borderPen(&dropBorder, 1);
        g.DrawRectangle(&borderPen, dropX, dropY, dropW, dropH);
        
        StringFormat itemFormat;
        itemFormat.SetAlignment(StringAlignmentNear);
        itemFormat.SetLineAlignment(StringAlignmentCenter);
        
        for (int i = 0; i < (int)menu.items.size(); ++i) {
            RECT itemRect = { dropX, dropY + i*itemH, dropX + dropW, dropY + (i+1)*itemH };
            if (PtInRect(&itemRect, mousePos)) {
                SolidBrush hoverBrush(Color(255, 59, 130, 246));
                g.FillRectangle(&hoverBrush, (INT)itemRect.left, (INT)itemRect.top, (INT)(itemRect.right - itemRect.left), (INT)(itemRect.bottom - itemRect.top));
            }
            g.DrawString(menu.items[i].name.c_str(), -1, &fontNormal, RectF((REAL)itemRect.left + 15, (REAL)itemRect.top, (REAL)(itemRect.right-itemRect.left - 15), (REAL)(itemRect.bottom-itemRect.top)), &itemFormat, &brushText);
        }
    }

    BitBlt(hdc, 0, 0, w, h, memDC, 0, 0, SRCCOPY);
    DeleteObject(memBitmap); DeleteDC(memDC);
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_DROPFILES: { // NEW: Handle native Drag and Drop
            HDROP hDrop = (HDROP)wParam;
            char filePath[MAX_PATH];
            if (DragQueryFileA(hDrop, 0, filePath, MAX_PATH)) {
                std::string file(filePath);
                if (file.find(".qnt") != std::string::npos) {
                    playFile(hwnd, file);
                } else if (file.find(".wav") != std::string::npos) {
                    encodeWavToQnt(file, getOutputPath(file, "-converted.qnt"));
                    MessageBoxA(hwnd, "WAV successfully encoded to QNT via Drag-and-Drop!", "Quantis", MB_OK | MB_ICONINFORMATION);
                }
            }
            DragFinish(hDrop);
            break;
        }
        case WM_GETMINMAXINFO: {
            LPMINMAXINFO lpMMI = (LPMINMAXINFO)lParam;
            lpMMI->ptMinTrackSize.x = 420; lpMMI->ptMinTrackSize.y = 550; return 0;
        }
        case WM_SIZE: {
            UpdateLayout(LOWORD(lParam), HIWORD(lParam));
            InvalidateRect(hwnd, NULL, FALSE); break;
        }
        case WM_MOUSEMOVE: {
            mousePos.x = LOWORD(lParam); mousePos.y = HIWORD(lParam);
            break;
        }
        case WM_LBUTTONDOWN: {
            if (openMenuIndex != -1) {
                auto& menu = menus[openMenuIndex];
                int dropW = 200; int itemH = 35;
                int dropX = menu.rect.left; int dropY = 30;
                
                bool clickedInsideDropdown = false;
                for (int i = 0; i < (int)menu.items.size(); ++i) {
                    RECT itemRect = { dropX, dropY + i*itemH, dropX + dropW, dropY + (i+1)*itemH };
                    if (PtInRect(&itemRect, mousePos)) {
                        HandleMenuAction(menu.items[i].id, hwnd);
                        clickedInsideDropdown = true;
                        break;
                    }
                }
                openMenuIndex = -1;
                InvalidateRect(hwnd, NULL, FALSE);
                if (clickedInsideDropdown) return 0;
            }
            
            for (int i = 0; i < (int)menus.size(); ++i) {
                if (PtInRect(&menus[i].rect, mousePos)) {
                    openMenuIndex = (openMenuIndex == i) ? -1 : i; 
                    InvalidateRect(hwnd, NULL, FALSE);
                    return 0;
                }
            }

            if (hasLoadedFile && PtInRect(&rectSlider, mousePos)) isDraggingSlider = true;
            break;
        }
        case WM_LBUTTONUP: {
            if (isDraggingSlider) {
                isDraggingSlider = false;
                if (songLengthMs > 0) {
                    float p = (float)(mousePos.x - rectSlider.left) / (rectSlider.right - rectSlider.left);
                    if(p < 0) p = 0; if(p > 1) p = 1;
                    songPosMs = (int)(p * songLengthMs);
                    std::string cmd = "seek qplayer to " + std::to_string(songPosMs);
                    mciSendStringA(cmd.c_str(), NULL, 0, NULL);
                    if(!isPaused) mciSendStringA("play qplayer", NULL, 0, NULL);
                }
            } else if (openMenuIndex == -1) { 
                if (PtInRect(&rectPlay, mousePos) && hasLoadedFile) {
                    if (isPaused) { mciSendStringA("play qplayer", NULL, 0, NULL); isPaused = false; }
                    else { mciSendStringA("pause qplayer", NULL, 0, NULL); isPaused = true; }
                }
                if (PtInRect(&rectEncode, mousePos)) {
                    std::string f = openFileDialog("WAV Audio\0*.wav\0", hwnd);
                    if (!f.empty()) {
                        encodeWavToQnt(f, getOutputPath(f, "-converted.qnt"));
                        MessageBoxA(hwnd, "WAV successfully encoded to QNT!", "Quantis", MB_OK | MB_ICONINFORMATION);
                    }
                }
                if (PtInRect(&rectDecode, mousePos)) {
                    std::string f = openFileDialog("Quantis Audio\0*.qnt\0", hwnd);
                    if (!f.empty()) {
                        decodeQntToWav(f, getOutputPath(f, "-converted.wav"));
                        MessageBoxA(hwnd, "QNT successfully decoded to WAV!", "Quantis", MB_OK | MB_ICONINFORMATION);
                    }
                }
            }
            break;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps; HDC hdc = BeginPaint(hwnd, &ps);
            DrawCustomUI(hwnd, hdc); EndPaint(hwnd, &ps); return 0;
        }
        case WM_TIMER: {
            if (!isPaused && hasLoadedFile && !isDraggingSlider) {
                char szPos[64]; mciSendStringA("status qplayer position", szPos, 64, NULL);
                if (strlen(szPos) > 0) songPosMs = std::stoi(szPos);
            }
            InvalidateRect(hwnd, NULL, FALSE); break;
        }
        case WM_DESTROY:
            mciSendStringA("close qplayer", NULL, 0, NULL);
            PostQuitMessage(0); return 0;
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR lpCmdLine, int nCmdShow) {
    GdiplusStartupInput gdiplusStartupInput; ULONG_PTR gdiplusToken;
    GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);

    const char CLASS_NAME[] = "QuantisWindow";
    WNDCLASS wc = {0}; wc.lpfnWndProc = WindowProc; wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME; wc.hbrBackground = NULL; 
    wc.hCursor = LoadCursor(NULL, IDC_ARROW); 
    RegisterClass(&wc);

    HWND hwnd = CreateWindowEx(0, CLASS_NAME, "Quantis Audio Player",
        WS_OVERLAPPEDWINDOW, 
        CW_USEDEFAULT, CW_USEDEFAULT, 500, 610, NULL, NULL, hInstance, NULL);

    if (hwnd == NULL) return 0;

    DragAcceptFiles(hwnd, TRUE); // NEW: Tells the OS we accept files dragged into our window

    BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    int corners = DWMWCP_ROUND;
    DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corners, sizeof(corners));

    ShowWindow(hwnd, nCmdShow);
    SetTimer(hwnd, 1, 16, NULL); 

    std::string cmdLine = lpCmdLine;
    if (!cmdLine.empty()) {
        cmdLine.erase(std::remove(cmdLine.begin(), cmdLine.end(), '\"'), cmdLine.end());
        if (cmdLine.find(".qnt") != std::string::npos) playFile(hwnd, cmdLine);
    }

    MSG msg = {0};
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg); DispatchMessage(&msg);
    }
    
    GdiplusShutdown(gdiplusToken);
    return 0;
}
