#pragma comment(lib, "ws2_32.lib")

#include <iostream>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <psapi.h>
#include <string>
#include <vector>
#include <fstream>
#include <tchar.h>

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 5001

#define SERVICE_NAME _T("ServiceName")

SERVICE_STATUS        g_ServiceStatus = { 0 };
SERVICE_STATUS_HANDLE g_ServiceStatusHandle = NULL;


// sc create ServiceName binPath= "<полный_путь_к_исполняемому_файлу>"
// sc start ServiceName
// sc stop ServiceName



class ActivityTracker {
public:
    void startTracking()
    {
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            std::cerr << "Failed to initialize Winsock." << std::endl;
            return;
        }

        while (true) {
            std::vector<std::string> activeApplications = getActiveApplications();
            std::vector<std::string> activeWindows =getActiveWindows();
            std::string dataApplication = prepareData(activeApplications);
            std::string dataWindows = prepareData(activeWindows);
            std::string dataActiveWindow = getActiveWindowTitle();
            
            sendDataToServer((dataApplication + "\n" + dataWindows + "\n" + dataActiveWindow).c_str(), "/api/workactivity");

            sendScreenshotToServer();
            
            Sleep(60000); 
        }

        WSACleanup();
    }

private:
    WSADATA wsaData;

    std::vector<std::string> getActiveApplications()
    {
        std::vector<std::string> activeApps;

        DWORD processIds[1024];
        DWORD bytesReturned;

        if (EnumProcesses(processIds, sizeof(processIds), &bytesReturned)) {
            DWORD numProcesses = bytesReturned / sizeof(DWORD);

            for (DWORD i = 0; i < numProcesses; ++i) {
                HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, processIds[i]);
                if (hProcess) {
                    char buffer[MAX_PATH];
                    if (GetModuleBaseNameA(hProcess, NULL, buffer, sizeof(buffer))) {
                        std::string processName(buffer);
                        activeApps.push_back(processName);
                    }
                    CloseHandle(hProcess);
                }
            }
        }

        return activeApps;
    }


    std::vector<std::string> getActiveWindows()
    {
        std::vector<std::string> activeApps;

        auto enumWindowsCallback = [](HWND hwnd, LPARAM lParam) -> BOOL {
            char buffer[256];
            if (IsWindowVisible(hwnd) && GetWindowTextA(hwnd, buffer, sizeof(buffer))) {
                std::string windowTitle(buffer);
                reinterpret_cast<std::vector<std::string>*>(lParam)->push_back(windowTitle);
            }
            return TRUE;
        };

        EnumWindows(enumWindowsCallback, reinterpret_cast<LPARAM>(&activeApps));

        return activeApps;
    }

    std::string getActiveWindowTitle()
    {
        HWND foregroundWindow = GetForegroundWindow();
        if (foregroundWindow != nullptr) {
            char windowTitle[256];
            if (GetWindowTextA(foregroundWindow, windowTitle, sizeof(windowTitle))) {
                return std::string(windowTitle);
            }
        }
        return std::string();
    }
    

    std::string prepareData(const std::vector<std::string>& activeApps)
    {
        std::string data;
        for (const auto& app : activeApps) {
            data += app + ", ";
        }
        data = data.substr(0, data.length() - 2);
        return data;
    }


    void sendScreenshotToServer() {
        HDC hdcScreen = GetDC(NULL);

        int screenWidth = GetSystemMetrics(SM_CXSCREEN);
        int screenHeight = GetSystemMetrics(SM_CYSCREEN);

        HDC hdcMem = CreateCompatibleDC(hdcScreen);

        HBITMAP hBitmap = CreateCompatibleBitmap(hdcScreen, screenWidth, screenHeight);

        SelectObject(hdcMem, hBitmap);

        BitBlt(hdcMem, 0, 0, screenWidth, screenHeight, hdcScreen, 0, 0, SRCCOPY);

        DeleteDC(hdcMem);
        ReleaseDC(NULL, hdcScreen);

        sendBitmapToServer(hBitmap);

        DeleteObject(hBitmap);
    }

    void sendBitmapToServer(HBITMAP hBitmap) {
        BITMAP bitmap;
        GetObject(hBitmap, sizeof(bitmap), &bitmap);

        BITMAPINFOHEADER bmih;
        bmih.biSize = sizeof(BITMAPINFOHEADER);
        bmih.biWidth = bitmap.bmWidth;
        bmih.biHeight = bitmap.bmHeight;
        bmih.biPlanes = 1;
        bmih.biBitCount = bitmap.bmBitsPixel;
        bmih.biCompression = BI_RGB;
        bmih.biSizeImage = 0;
        bmih.biXPelsPerMeter = 0;
        bmih.biYPelsPerMeter = 0;
        bmih.biClrUsed = 0;
        bmih.biClrImportant = 0;

        int bufferSize = sizeof(BITMAPINFOHEADER) + bitmap.bmWidth * bitmap.bmHeight * (bitmap.bmBitsPixel / 8);
        char* buffer = new char[bufferSize];

        memcpy(buffer, &bmih, sizeof(BITMAPINFOHEADER));
        GetBitmapBits(hBitmap, bitmap.bmWidth * bitmap.bmHeight * (bitmap.bmBitsPixel / 8), buffer + sizeof(BITMAPINFOHEADER));

        sendDataToServer(buffer, bufferSize, "/api/screenshot");

        delete[] buffer;
    }


    void sendDataToServer(const char* data, int size, std::string urlPath) {
        sendDataToServerHelper(data, size, urlPath);
    }

    void sendDataToServer(const char* data, std::string urlPath) {
        sendDataToServerHelper(data, strlen(data), urlPath);
    }
    

    void sendDataToServerHelper(const char* data, int size, std::string urlPath) {
        SOCKET clientSocket = socket(AF_INET, SOCK_STREAM, 0);
        if (clientSocket == INVALID_SOCKET) {
            std::cerr << "Failed to create socket 2." << std::endl;
            return;
        }
        
        sockaddr_in serverAddress{};
        serverAddress.sin_family = AF_INET;
        serverAddress.sin_port = htons(SERVER_PORT);
        if (inet_pton(AF_INET, SERVER_IP, &(serverAddress.sin_addr)) <= 0) {
            std::cerr << "Invalid address: " << SERVER_IP << std::endl;
            closesocket(clientSocket);
            return;
        }

        if (connect(clientSocket, reinterpret_cast<sockaddr*>(&serverAddress), sizeof(serverAddress)) < 0) {
            std::cerr << "Connection failed." << std::endl;
            closesocket(clientSocket);
            return;
        }

        std::string httpRequest = "POST " + urlPath + " HTTP/1.1\r\n";
        httpRequest += "Host: " + std::string(SERVER_IP) + "\r\n";
        httpRequest += "Content-Length: " + std::to_string(size) + "\r\n";
        httpRequest += "Content-Type: text/plain\r\n";
        httpRequest += "\r\n";
        httpRequest += data;

        if (send(clientSocket, httpRequest.c_str(), httpRequest.length(), 0) < 0) {
            std::cerr << "Failed to send data." << std::endl;
        }

        char buffer[1024];
        int bytesRead;
        std::string response;

        while ((bytesRead = recv(clientSocket, buffer, sizeof(buffer) - 1, 0)) > 0) {
            buffer[bytesRead] = '\0';
            response += buffer;

            size_t found = response.find("\r\n\r\n");
            if (found != std::string::npos) {
                break;
            }
        }

        std::cout << "Response from server:\n" << response << std::endl;

        closesocket(clientSocket);
    }
};



class ActivityTrackerService {
public:
    static void WINAPI ServiceMain(DWORD argc, LPTSTR* argv)
    {
        g_ServiceStatusHandle = RegisterServiceCtrlHandler(SERVICE_NAME, ServiceCtrlHandler);
        if (!g_ServiceStatusHandle)
        {
            return;
        }

        SetCurrentServiceStatus(SERVICE_START_PENDING);

        ActivityTracker tracker;

        tracker.startTracking();

        // SetCurrentServiceStatus(SERVICE_STOPPED);
    }

    static void WINAPI ServiceCtrlHandler(DWORD ctrlCode)
    {
        switch (ctrlCode)
        {
        case SERVICE_CONTROL_STOP:
            SetCurrentServiceStatus(SERVICE_STOP_PENDING);
            SetCurrentServiceStatus(SERVICE_STOPPED);
            break;

        }

    }

private:

    static void SetCurrentServiceStatus(DWORD dwCurrentState)
    {
        g_ServiceStatus.dwCurrentState = dwCurrentState;
        SetServiceStatus(g_ServiceStatusHandle, &g_ServiceStatus);
    }
};


// int main()
// {
//     ActivityTracker tracker{};
//     tracker.startTracking();
//
//     return 0;   
// }