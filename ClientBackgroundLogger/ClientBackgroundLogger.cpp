#pragma comment(lib, "ws2_32.lib")

#include <iostream>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <psapi.h>
#include <string>
#include <vector>

#define SERVER_IP "127.0.0.1/api/workactivity"
#define SERVER_PORT 5001



class ActivityTracker {
public:
    void startTracking()
    {
        // Инициализация Winsock
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
            
            sendDataToServer((dataApplication + "\n" + dataWindows + "\n" + dataActiveWindow).c_str());
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

    void sendDataToServer(const char* data)
    {
        SOCKET clientSocket = socket(AF_INET, SOCK_STREAM, 0);
        if (clientSocket == INVALID_SOCKET) {
            std::cerr << "Failed to create socket 2." << std::endl;
            return;
        }

        sockaddr_in serverAddress{};
        serverAddress.sin_family = AF_INET;
        serverAddress.sin_port = htons(SERVER_PORT);
        if (inet_pton(AF_INET, "127.0.0.1", &(serverAddress.sin_addr)) <= 0) {
            std::cerr << "Invalid address: 127.0.0.1" << std::endl;
            closesocket(clientSocket);
            return;
        }

        if (connect(clientSocket, reinterpret_cast<sockaddr*>(&serverAddress), sizeof(serverAddress)) < 0) {
            std::cerr << "Connection failed." << std::endl;
            closesocket(clientSocket);
            return;
        }
        
        std::string httpRequest = "POST /api/workactivity HTTP/1.1\r\n";
        httpRequest += "Host: 127.0.0.1\r\n";  // Замените на ваш хост
        httpRequest += "Content-Length: " + std::to_string(strlen(data)) + "\r\n";
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

int main()
{
    ActivityTracker tracker{};
    tracker.startTracking();

    return 0;   
}
