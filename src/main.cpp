#pragma comment(linker,"\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

#define NOMINMAX
#include <windows.h>
#include <mmsystem.h>
#include <dwmapi.h>
#include <commctrl.h>
#include <commdlg.h>
#include <fstream>
#include <vector>
#include <string>
#include <cmath>
#include <algorithm>
#include <stdexcept>

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

HWND hTrackbar = NULL;
bool isSystemDarkMode() {
    DWORD useLightTheme = 1;
    DWORD dataSize = sizeof(useLightTheme);
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        RegQueryValueExA(hKey, "AppsUseLightTheme", NULL, NULL, reinterpret_cast<LPBYTE>(&useLightTheme), &dataSize);
        RegCloseKey(hKey);
    }
    return useLightTheme == 0; 
}
bool darkMode = false;
bool isPaused = false; 

struct QuantisHeader {
    char magic[4] = {'Q', 'N', 'T', '1'};
    uint32_t sampleRate = 0; uint16_t channels = 0;
    uint16_t bitDepth = 0; uint32_t dataSize = 0; 
};

std::string encodeWavToQnt(const std::string& inputFile, const std::string& outputFile) {
    try {
        std::ifstream wavFile(inputFile, std::ios::binary);
        if (!wavFile) return "Could not open source file.";
        char riff[4] = {0}, wave[4] = {0}; uint32_t fileSize = 0;
        wavFile.read(riff, 4); wavFile.read(reinterpret_cast<char*>(&fileSize), 4); wavFile.read(wave, 4);
        if (std::string(riff, 4) != "RIFF" || std::string(wave, 4) != "WAVE") return "Not a valid WAV file!";
        
        uint16_t numChannels = 0, bitsPerSample = 0, audioFormat = 0; uint32_t sampleRate = 0;
        std::vector<char> rawAudio;

        while (true) {
            char chunkId[4] = {0}; uint32_t chunkSize = 0;
            wavFile.read(chunkId, 4); if (wavFile.gcount() != 4) break; 
            wavFile.read(reinterpret_cast<char*>(&chunkSize), 4); if (wavFile.gcount() != 4) break;
            std::string id(chunkId, 4);
            
            if (id == "fmt ") {
                if (chunkSize < 16) return "Corrupted format block.";
                wavFile.read(reinterpret_cast<char*>(&audioFormat), 2);
                wavFile.read(reinterpret_cast<char*>(&numChannels), 2);
                wavFile.read(reinterpret_cast<char*>(&sampleRate), 4);
                wavFile.seekg(6, std::ios::cur); wavFile.read(reinterpret_cast<char*>(&bitsPerSample), 2);
                if (chunkSize > 16) wavFile.seekg(chunkSize - 16, std::ios::cur);
                if (chunkSize % 2 != 0) wavFile.seekg(1, std::ios::cur);
            } else if (id == "data") {
                rawAudio.resize(chunkSize); wavFile.read(rawAudio.data(), chunkSize); break;
            } else {
                wavFile.seekg(chunkSize, std::ios::cur);
                if (chunkSize % 2 != 0) wavFile.seekg(1, std::ios::cur);
            }
        }
        wavFile.close();
        if (rawAudio.empty()) return "No audio data found.";
        if (numChannels < 1 || numChannels > 2) return "Only Mono or Stereo supported.";

        std::vector<int16_t> pcmData;
        if (audioFormat == 1 && bitsPerSample == 16) {
            size_t numSamples = rawAudio.size() / 2;
            int16_t* pRaw = reinterpret_cast<int16_t*>(rawAudio.data());
            pcmData.assign(pRaw, pRaw + numSamples);
        }
        else if (audioFormat == 1 && bitsPerSample == 24) {
            size_t numSamples = rawAudio.size() / 3; pcmData.reserve(numSamples);
            for (size_t i = 0; i < rawAudio.size(); i += 3) {
                int32_t sample = (static_cast<uint8_t>(rawAudio[i]) | (static_cast<uint8_t>(rawAudio[i+1]) << 8) | (static_cast<int8_t>(rawAudio[i+2]) << 16)); 
                pcmData.push_back(static_cast<int16_t>(sample >> 8));
            }
        }
        else if (audioFormat == 3 && bitsPerSample == 32) {
            size_t numSamples = rawAudio.size() / 4; pcmData.reserve(numSamples);
            float* pRaw = reinterpret_cast<float*>(rawAudio.data());
            for (size_t i = 0; i < numSamples; ++i) {
                float fSample = pRaw[i];
                if (fSample > 1.0f) fSample = 1.0f; if (fSample < -1.0f) fSample = -1.0f;
                pcmData.push_back(static_cast<int16_t>(fSample * 32767.0f));
            }
        }
        else return "Unsupported format. Need 16-bit, 24-bit, or 32-bit float.";

        size_t numSamples = pcmData.size(); std::vector<char> compressedStream;
        const int BLOCK_SIZE = 500; compressedStream.reserve(numSamples + (numSamples / BLOCK_SIZE) + 10);
        int16_t previous[2] = {0, 0}; 

        for (size_t i = 0; i < numSamples; i += BLOCK_SIZE) {
            size_t currentBlockSize = std::min((size_t)BLOCK_SIZE, numSamples - i);
            int32_t maxDiff = 0; int16_t simPrev[2] = {previous[0], previous[1]};
            for (size_t j = 0; j < currentBlockSize; ++j) {
                int channel = (numChannels == 2) ? (j % 2) : 0;
                int32_t diff = std::abs(static_cast<int32_t>(pcmData[i + j]) - simPrev[channel]);
                if (diff > maxDiff) maxDiff = diff;
                simPrev[channel] = pcmData[i + j];
            }
            int32_t neededScale = (maxDiff / 127) + 1;
            if (neededScale > 255) neededScale = 255; 
            uint8_t scale = static_cast<uint8_t>(neededScale);
            compressedStream.push_back(static_cast<char>(scale));

            for (size_t j = 0; j < currentBlockSize; ++j) {
                int channel = (numChannels == 2) ? (j % 2) : 0;
                int32_t diff = static_cast<int32_t>(pcmData[i + j]) - previous[channel];
                int32_t quantized = diff / scale; 
                if (quantized > 127) quantized = 127; if (quantized < -128) quantized = -128;
                compressedStream.push_back(static_cast<char>(quantized));
                previous[channel] += (quantized * scale);
            }
        }

        QuantisHeader qntHeader; qntHeader.sampleRate = sampleRate; qntHeader.channels = numChannels;
        qntHeader.bitDepth = 16; qntHeader.dataSize = compressedStream.size();
        std::ofstream qntFile(outputFile, std::ios::binary);
        if (!qntFile) return "No write permission.";
        qntFile.write(reinterpret_cast<const char*>(&qntHeader), sizeof(qntHeader));
        qntFile.write(compressedStream.data(), compressedStream.size());
        qntFile.close();
        return ""; 
    } catch (...) { return "Error during compression."; }
}

std::string decodeQntToWav(const std::string& inputFile, const std::string& outputFile) {
    try {
        std::ifstream qntFile(inputFile, std::ios::binary);
        if (!qntFile) return "Could not open Quantis file.";
        QuantisHeader header; qntFile.read(reinterpret_cast<char*>(&header), sizeof(QuantisHeader));
        if (std::string(header.magic, 4) != "QNT1") return "Not a valid .qnt file!";
        std::vector<char> compressedStream(header.dataSize);
        qntFile.read(compressedStream.data(), header.dataSize); qntFile.close();

        std::vector<int16_t> pcmData; const int BLOCK_SIZE = 500;
        int16_t previous[2] = {0, 0}; size_t byteIndex = 0; pcmData.reserve(header.dataSize * 2);

        while (byteIndex < compressedStream.size()) {
            uint8_t scale = static_cast<uint8_t>(compressedStream[byteIndex++]);
            size_t currentBlockSize = std::min((size_t)BLOCK_SIZE, compressedStream.size() - byteIndex);
            for (size_t j = 0; j < currentBlockSize; ++j) {
                int channel = (header.channels == 2) ? (j % 2) : 0;
                int8_t quantized = static_cast<int8_t>(compressedStream[byteIndex++]);
                int32_t current = previous[channel] + (quantized * scale);
                if (current > 32767) current = 32767; if (current < -32768) current = -32768;
                pcmData.push_back(static_cast<int16_t>(current));
                previous[channel] = pcmData.back();
            }
        }

        std::ofstream wavFile(outputFile, std::ios::binary);
        if (!wavFile) return "Could not save decoded WAV.";
        wavFile.write("RIFF", 4);
        uint32_t uncompressedSize = pcmData.size() * 2; uint32_t totalFileSize = 36 + uncompressedSize;
        wavFile.write(reinterpret_cast<char*>(&totalFileSize), 4);
        wavFile.write("WAVE", 4); wavFile.write("fmt ", 4);
        uint32_t fmtSize = 16; wavFile.write(reinterpret_cast<char*>(&fmtSize), 4);
        uint16_t audioFormat = 1; wavFile.write(reinterpret_cast<char*>(&audioFormat), 2);
        wavFile.write(reinterpret_cast<char*>(&header.channels), 2);
        wavFile.write(reinterpret_cast<char*>(&header.sampleRate), 4);
        uint32_t byteRate = header.sampleRate * header.channels * 2;
        wavFile.write(reinterpret_cast<char*>(&byteRate), 4);
        uint16_t blockAlign = header.channels * 2; wavFile.write(reinterpret_cast<char*>(&blockAlign), 2);
        wavFile.write(reinterpret_cast<char*>(&header.bitDepth), 2);
        wavFile.write("data", 4); wavFile.write(reinterpret_cast<char*>(&uncompressedSize), 4);
        wavFile.write(reinterpret_cast<char*>(pcmData.data()), uncompressedSize);
        wavFile.close();
        return ""; 
    } catch (...) { return "Error during decoding."; }
}

std::string openFileDialog(const char* filter, HWND owner) {
    OPENFILENAMEA ofn; char szFile[260] = {0}; ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = owner; ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile); ofn.lpstrFilter = filter; ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameA(&ofn)) return szFile; return "";
}

std::string getOutputPath(const std::string& inputPath, const std::string& newExtension) {
    size_t dotPos = inputPath.find_last_of('.');
    if (dotPos == std::string::npos) return inputPath + newExtension;
    return inputPath.substr(0, dotPos) + newExtension;
}

std::string getTempPlayPath(const std::string& originalFile) {
    char tempPath[MAX_PATH];
    GetTempPathA(MAX_PATH, tempPath);
    
    std::string quantisTemp = std::string(tempPath) + "Quantis\\";
    CreateDirectoryA(quantisTemp.c_str(), NULL);
    
    size_t slashPos = originalFile.find_last_of("/\\");
    std::string fileName = (slashPos == std::string::npos) ? originalFile : originalFile.substr(slashPos + 1);
    
    return quantisTemp + fileName + "_temp.wav";
}

void playQuantisFile(HWND hwnd, const std::string& file) {
    std::string tempFile = getOutputPath(file, "_temp_play.wav");
    SetWindowTextA(hwnd, "QUANTIS - Loading...");
    std::string error = decodeQntToWav(file, tempFile);
    SetWindowTextA(hwnd, "Quantis Audio");
    
    if (error.empty()) {
        mciSendStringA("close qplayer", NULL, 0, NULL); 
        std::string openCmd = "open \"" + tempFile + "\" type waveaudio alias qplayer";
        mciSendStringA(openCmd.c_str(), NULL, 0, NULL);
        mciSendStringA("set qplayer time format milliseconds", NULL, 0, NULL);
        
        char szLen[64];
        mciSendStringA("status qplayer length", szLen, 64, NULL);
        if (strlen(szLen) > 0) {
            SendMessage(hTrackbar, TBM_SETRANGEMAX, TRUE, std::stoi(szLen));
            SendMessage(hTrackbar, TBM_SETPOS, TRUE, 0);
        }

        mciSendStringA("play qplayer", NULL, 0, NULL);
        isPaused = false;
    } else {
        MessageBoxA(hwnd, error.c_str(), "Playback Error", MB_OK | MB_ICONERROR);
    }
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        
        case WM_DRAWITEM: {
            LPDRAWITEMSTRUCT pdis = (LPDRAWITEMSTRUCT)lParam;
            if (pdis->CtlType == ODT_BUTTON) {
                SetBkMode(pdis->hDC, TRANSPARENT);
                
                HBRUSH bgBrush;
                if (darkMode) {
                    if (pdis->itemState & ODS_SELECTED) bgBrush = CreateSolidBrush(RGB(100, 100, 100));
                    else bgBrush = CreateSolidBrush(RGB(50, 50, 50));
                    SetTextColor(pdis->hDC, RGB(255, 255, 255));
                } else {
                    if (pdis->itemState & ODS_SELECTED) bgBrush = CreateSolidBrush(RGB(180, 180, 180));
                    else bgBrush = CreateSolidBrush(RGB(230, 230, 230)); 
                    SetTextColor(pdis->hDC, RGB(0, 0, 0));
                }
                
                FillRect(pdis->hDC, &pdis->rcItem, bgBrush);
                DeleteObject(bgBrush);

                char text[256];
                GetWindowTextA(pdis->hwndItem, text, 256);
                DrawTextA(pdis->hDC, text, -1, &pdis->rcItem, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                
                return TRUE;
            }
            break;
        }

        case WM_TIMER: {
            if (!isPaused && hTrackbar != NULL) {
                char szPos[64];
                mciSendStringA("status qplayer position", szPos, 64, NULL);
                if (strlen(szPos) > 0) {
                    SendMessage(hTrackbar, TBM_SETPOS, TRUE, std::stoi(szPos));
                }
            }
            break;
        }

        case WM_HSCROLL: {
            if ((HWND)lParam == hTrackbar) {
                int pos = SendMessage(hTrackbar, TBM_GETPOS, 0, 0);
                std::string seekCmd = "seek qplayer to " + std::to_string(pos);
                mciSendStringA(seekCmd.c_str(), NULL, 0, NULL);
                if (!isPaused) mciSendStringA("play qplayer", NULL, 0, NULL);
            }
            break;
        }

        case WM_COMMAND:
            if (LOWORD(wParam) == 3) {
                std::string file = openFileDialog("Quantis Audio\0*.qnt\0", hwnd);
                if (!file.empty()) playQuantisFile(hwnd, file);
            }
            if (LOWORD(wParam) == 4) {
                if (!isPaused) { mciSendStringA("pause qplayer", NULL, 0, NULL); isPaused = true; } 
                else { mciSendStringA("resume qplayer", NULL, 0, NULL); isPaused = false; }
            }
            if (LOWORD(wParam) == 1) {
                std::string file = openFileDialog("WAV Audio\0*.wav\0", hwnd);
                if (!file.empty()) {
                    std::string out = getOutputPath(file, ".qnt");
                    SetWindowTextA(hwnd, "Working... Encoding...");
                    std::string error = encodeWavToQnt(file, out);
                    SetWindowTextA(hwnd, "Quantis Audio");
                    if (error.empty()) MessageBoxA(hwnd, ("Saved:\n" + out).c_str(), "Success!", MB_OK | MB_ICONINFORMATION);
                    else MessageBoxA(hwnd, error.c_str(), "Error", MB_OK | MB_ICONERROR);
                }
            }
            break;
        case WM_DESTROY:
            KillTimer(hwnd, 1);
            mciSendStringA("close qplayer", NULL, 0, NULL); 
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_WIN95_CLASSES;
    InitCommonControlsEx(&icex);

    darkMode = isSystemDarkMode();

    const char CLASS_NAME[] = "QuantisWindow";
    WNDCLASS wc = {0};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hbrBackground = darkMode ? CreateSolidBrush(RGB(30, 30, 30)) : (HBRUSH)(COLOR_WINDOW);
    RegisterClass(&wc);

    HWND hwnd = CreateWindowEx(0, CLASS_NAME, "Quantis Audio",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, 
        CW_USEDEFAULT, CW_USEDEFAULT, 420, 360,
        NULL, NULL, hInstance, NULL);

    if (hwnd == NULL) return 0;

    if (darkMode) {
        BOOL value = TRUE;
        DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &value, sizeof(value));
    }

    HFONT hFont = CreateFontA(18, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, ANSI_CHARSET, 
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, "Segoe UI");

    HWND btnPlay = CreateWindow("BUTTON", "LOAD & PLAY QUANTIS FILE", WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
        20, 20, 360, 50, hwnd, (HMENU)3, hInstance, NULL);
    
    hTrackbar = CreateWindowEx(0, TRACKBAR_CLASS, "Trackbar",
        WS_CHILD | WS_VISIBLE | TBS_AUTOTICKS | TBS_ENABLESELRANGE,
        15, 80, 370, 30, hwnd, (HMENU)10, hInstance, NULL);

    HWND btnPause = CreateWindow("BUTTON", "PAUSE / RESUME", WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
        20, 120, 360, 35, hwnd, (HMENU)4, hInstance, NULL);

    HWND label = CreateWindow("STATIC", " Developer Tools:", WS_VISIBLE | WS_CHILD,
        20, 190, 360, 25, hwnd, NULL, hInstance, NULL);
    
    HWND btnEnc = CreateWindow("BUTTON", "Encode WAV to QNT", WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
        20, 220, 175, 40, hwnd, (HMENU)1, hInstance, NULL);

    SendMessage(btnPlay, WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessage(btnPause, WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessage(btnEnc, WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessage(label, WM_SETFONT, (WPARAM)hFont, TRUE);
    
    if (darkMode) {  }

    ShowWindow(hwnd, nCmdShow);

    SetTimer(hwnd, 1, 100, NULL);

    std::string cmdLine = lpCmdLine;
    if (!cmdLine.empty()) {
        cmdLine.erase(std::remove(cmdLine.begin(), cmdLine.end(), '\"'), cmdLine.end());
        if (cmdLine.find(".qnt") != std::string::npos) {
            playQuantisFile(hwnd, cmdLine);
        }
    }

    MSG msg = {0};
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    
    DeleteObject(hFont);
    return 0;
}
