#define _CRT_SECURE_NO_WARNINGS
#define _WINSOCK_DEPRECATED_NO_WARNINGS 
#include <iostream>
#include <string>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <fstream>
#include <filesystem>
#include <vector>
#include <chrono>
#include <thread>
#include <windows.h>
#include <mutex>
namespace fs = std::filesystem;

#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "gdi32.lib")

class Vector2 {
public:
    Vector2() : x(0.0), y(0.0) {}
    Vector2(double _x, double _y) : x(_x), y(_y) {}
    ~Vector2() {}
    double x, y;
};

class Vector3 {
public:
    Vector3() : x(0.0), y(0.0), z(0.0) {}
    Vector3(double _x, double _y, double _z) : x(_x), y(_y), z(_z) {}
    ~Vector3() {}
    double x, y, z;

    float dot(Vector3 v) { return x * v.x + y * v.y + z * v.z; }
    float distance(Vector3 v) { return sqrt((v.x - x) * (v.x - x) + (v.y - y) * (v.y - y) + (v.z - z) * (v.z - z)); }
    Vector3 normalized() const { float mag = magnitude(); return Vector3(x / mag, y / mag, z / mag); }
    float magnitude() const { return sqrt(x * x + y * y + z * z); }
    Vector3 operator+(Vector3 v) { return Vector3(x + v.x, y + v.y, z + v.z); }
    Vector3 operator-(Vector3 v) { return Vector3(x - v.x, y - v.y, z - v.z); }
    Vector3 operator*(float number) const { return Vector3(x * number, y * number, z * number); }
};

// Set socket options
void set_socket_options(SOCKET sock, int timeout_ms) {
    DWORD timeout = timeout_ms;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout));
    int buffer_size = 8 * 1024 * 1024; // 8MB
    setsockopt(sock, SOL_SOCKET, SO_SNDBUF, (const char*)&buffer_size, sizeof(buffer_size));
    setsockopt(sock, SOL_SOCKET, SO_RCVBUF, (const char*)&buffer_size, sizeof(buffer_size));
}

// Check if socket is valid
bool is_socket_valid(SOCKET sock) {
    int error = 0;
    int len = sizeof(error);
    return getsockopt(sock, SOL_SOCKET, SO_ERROR, (char*)&error, &len) == 0 && error == 0;
}

// Clear socket buffer
void clear_socket_buffer(SOCKET sock) {
    char temp_buffer[1024];
    DWORD timeout = 100;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
    while (recv(sock, temp_buffer, sizeof(temp_buffer), 0) > 0) {}
    set_socket_options(sock, 5000); // Restore default timeout
}

// Reconnect to server
SOCKET reconnect(struct sockaddr_in& serv_addr, const std::string& ip_address) {
    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        std::cerr << "Socket creation error: " << WSAGetLastError() << "\n";
        return INVALID_SOCKET;
    }
    set_socket_options(sock, 5000);
    if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) == SOCKET_ERROR) {
        std::cerr << "Reconnection failed: " << WSAGetLastError() << "\n";
        closesocket(sock);
        return INVALID_SOCKET;
    }
    char buffer[4096] = { 0 };
    int valread = recv(sock, buffer, sizeof(buffer) - 1, 0);
    if (valread > 0) {
        buffer[valread] = '\0';
        std::cout << "Reconnected to server: " << buffer << "\n";
    }
    else {
        std::cerr << "Failed to receive server acknowledgment: " << WSAGetLastError() << "\n";
        closesocket(sock);
        return INVALID_SOCKET;
    }
    return sock;
}

// Global variables for window and bitmap
HBITMAP hBitmap = NULL;
int bmp_width = 0, bmp_height = 0;
HWND hwnd = NULL;
std::mutex bmp_mutex;
std::string error_message;

// Decompress RLE data
bool decompress_rle(const unsigned char* input, int input_size, std::vector<unsigned char>& output, int expected_size) {
    output.clear();
    output.reserve(expected_size);
    for (int i = 0; i < input_size && output.size() < expected_size;) {
        if (i + 3 >= input_size) {
            std::cerr << "Incomplete RLE data at index " << i << "\n";
            return false;
        }
        unsigned char count = input[i++];
        unsigned char r = input[i++];
        unsigned char g = input[i++];
        unsigned char b = input[i++];
        for (int j = 0; j < count && output.size() < expected_size; j++) {
            output.push_back(r);
            output.push_back(g);
            output.push_back(b);
        }
    }
    if (output.size() != expected_size) {
        std::cerr << "RLE decompressed to " << output.size() << " bytes, expected " << expected_size << "\n";
        output.resize(expected_size, 128); // Pad with gray
        return false;
    }
    return true;
}

// Convert RGB to bitmap
HBITMAP create_bitmap_from_rgb(const std::vector<unsigned char>& rgb_data, int width, int height) {
    BITMAPINFO bmi = { 0 };
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 24;
    bmi.bmiHeader.biCompression = BI_RGB;

    HDC hdc = GetDC(NULL);
    void* bmp_bits;
    HBITMAP hBitmap = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, &bmp_bits, NULL, 0);
    if (hBitmap && rgb_data.size() >= width * height * 3) {
        memcpy(bmp_bits, rgb_data.data(), width * height * 3);
    }
    else {
        std::cerr << "Failed to create bitmap: invalid data size " << rgb_data.size() << "\n";
    }
    ReleaseDC(NULL, hdc);
    return hBitmap;
}

// Window procedure
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        std::lock_guard<std::mutex> lock(bmp_mutex);
        if (hBitmap) {
            HDC hdcMem = CreateCompatibleDC(hdc);
            HBITMAP hOldBitmap = (HBITMAP)SelectObject(hdcMem, hBitmap);
            RECT rect;
            GetClientRect(hwnd, &rect);
            int win_width = rect.right - rect.left;
            int win_height = rect.bottom - rect.top;
            SetStretchBltMode(hdc, HALFTONE);
            StretchBlt(hdc, 0, 0, win_width, win_height, hdcMem, 0, 0, bmp_width, bmp_height, SRCCOPY);
            SelectObject(hdcMem, hOldBitmap);
            DeleteDC(hdcMem);
        }
        else {
            HBRUSH brush = CreateSolidBrush(RGB(128, 128, 128));
            FillRect(hdc, &ps.rcPaint, brush);
            DeleteObject(brush);
            if (!error_message.empty()) {
                SetTextColor(hdc, RGB(255, 255, 255));
                SetBkColor(hdc, RGB(128, 128, 128));
                TextOutA(hdc, 10, 10, error_message.c_str(), error_message.size());
            }
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

// Create display window
HWND create_window(int width, int height) {
    const char* CLASS_NAME = "ScreenShareWindow";
    WNDCLASS wc = { 0 };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = CLASS_NAME;
    RegisterClass(&wc);

    HWND hwnd = CreateWindow(
        CLASS_NAME,
        "PC Principal Screen",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, width, height,
        NULL, NULL, GetModuleHandle(NULL), NULL
    );

    if (hwnd == NULL) {
        std::cerr << "Failed to create window: " << GetLastError() << "\n";
        return NULL;
    }

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    return hwnd;
}

// Receive and display screen stream
void receive_screen_stream(SOCKET& sock, volatile bool& stop_streaming, int target_width, int target_height, const std::string& ip_address, struct sockaddr_in& serv_addr) {
    set_socket_options(sock, 5000); // 5s timeout
    char* buffer = new char[8 * 1024 * 1024]; // 8MB buffer
    int window_width = target_width, window_height = target_height;
    int frame_count = 0;
    auto stream_start = std::chrono::steady_clock::now();
    auto last_frame_time = stream_start;

    hwnd = create_window(window_width, window_height);
    if (!hwnd) {
        std::cerr << "Failed to create display window\n";
        stop_streaming = true;
        delete[] buffer;
        return;
    }

    MSG msg = { 0 };
    while (!stop_streaming) {
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if (msg.message == WM_QUIT) {
                stop_streaming = true;
                break;
            }
        }
        if (stop_streaming) break;

        if (!is_socket_valid(sock)) {
            std::cerr << "Socket invalid, attempting reconnect\n";
            closesocket(sock);
            sock = reconnect(serv_addr, ip_address);
            if (sock == INVALID_SOCKET) {
                error_message = "Connection lost. Reconnect failed.";
                InvalidateRect(hwnd, NULL, TRUE);
                UpdateWindow(hwnd);
                std::this_thread::sleep_for(std::chrono::milliseconds(2000));
                continue;
            }
            set_socket_options(sock, 5000);
            char type = 7;
            int resolution[2] = { target_width, target_height };
            if (send(sock, &type, 1, 0) == SOCKET_ERROR || send(sock, (char*)resolution, sizeof(resolution), 0) == SOCKET_ERROR) {
                std::cerr << "Failed to resend screen command: " << WSAGetLastError() << "\n";
                error_message = "Failed to restart streaming";
                InvalidateRect(hwnd, NULL, TRUE);
                UpdateWindow(hwnd);
                continue;
            }
            std::cout << "Re-sent screen command after reconnect\n";
            clear_socket_buffer(sock);
        }

        char type;
        int valread = recv(sock, &type, 1, 0);
        if (valread != 1) {
            std::cerr << "Failed to receive type: " << WSAGetLastError() << "\n";
            error_message = "Waiting for server... (Error " + std::to_string(WSAGetLastError()) + ")";
            InvalidateRect(hwnd, NULL, TRUE);
            UpdateWindow(hwnd);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        if (type != 7) {
            std::cerr << "Unexpected type received: " << (int)type << "\n";
            error_message = "Invalid server response";
            InvalidateRect(hwnd, NULL, TRUE);
            UpdateWindow(hwnd);
            continue;
        }

        int header[3];
        valread = recv(sock, (char*)header, sizeof(header), 0);
        if (valread != sizeof(header)) {
            std::cerr << "Failed to receive header: " << WSAGetLastError() << "\n";
            error_message = "Waiting for server... (Error " + std::to_string(WSAGetLastError()) + ")";
            InvalidateRect(hwnd, NULL, TRUE);
            UpdateWindow(hwnd);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        int width = header[0], height = header[1], size = header[2];
        if (size > 8 * 1024 * 1024 || width <= 0 || height <= 0 || size <= 0) {
            std::cerr << "Invalid header: width=" << width << ", height=" << height << ", size=" << size << "\n";
            error_message = "Invalid frame data";
            InvalidateRect(hwnd, NULL, TRUE);
            UpdateWindow(hwnd);
            continue;
        }

        int total_received = 0;
        int retries = 3;
        while (total_received < size && retries > 0) {
            valread = recv(sock, buffer + total_received, size - total_received, 0);
            if (valread <= 0) {
                std::cerr << "Failed to receive data for frame " << frame_count << ": " << WSAGetLastError() << " (retry " << (4 - retries) << "/3)\n";
                retries--;
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            else {
                total_received += valread;
                retries = 3; // Reset retries on successful read
            }
        }
        if (total_received != size) {
            std::cerr << "Incomplete data for frame " << frame_count << ": received " << total_received << "/" << size << " bytes\n";
            error_message = "Incomplete frame received";
            InvalidateRect(hwnd, NULL, TRUE);
            UpdateWindow(hwnd);
            continue;
        }

        std::vector<unsigned char> rgb_data;
        if (!decompress_rle((unsigned char*)buffer, size, rgb_data, width * height * 3)) {
            std::cerr << "RLE decompression failed for frame " << frame_count << "\n";
            error_message = "Failed to decompress frame";
            InvalidateRect(hwnd, NULL, TRUE);
            UpdateWindow(hwnd);
            continue;
        }

        HBITMAP newBitmap = create_bitmap_from_rgb(rgb_data, width, height);
        if (newBitmap) {
            std::lock_guard<std::mutex> lock(bmp_mutex);
            if (hBitmap) DeleteObject(hBitmap);
            hBitmap = newBitmap;
            bmp_width = width;
            bmp_height = height;
            error_message.clear();
            InvalidateRect(hwnd, NULL, TRUE);
            UpdateWindow(hwnd);
            std::cout << "Displayed frame " << frame_count << ": " << size << " bytes (" << width << "x" << height << ")\n";
            frame_count++;
            last_frame_time = std::chrono::steady_clock::now();
        }

        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - last_frame_time).count();
        if (elapsed > 5000) {
            error_message = "No frames received recently";
            InvalidateRect(hwnd, NULL, TRUE);
            UpdateWindow(hwnd);
        }
    }

    std::lock_guard<std::mutex> lock(bmp_mutex);
    if (hBitmap) DeleteObject(hBitmap);
    hBitmap = NULL;
    if (hwnd) DestroyWindow(hwnd);
    hwnd = NULL;
    stop_streaming = true;
    delete[] buffer;
}

int main() {
    WSADATA wsaData;
    SOCKET sock = INVALID_SOCKET;
    struct sockaddr_in serv_addr;
    char buffer[16384] = { 0 };
    std::string ip_address;

    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed: " << WSAGetLastError() << "\n";
        return 1;
    }

    std::cout << "Enter server IP address: ";
    std::getline(std::cin, ip_address);

    sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        std::cerr << "Socket creation error: " << WSAGetLastError() << "\n";
        WSACleanup();
        return 1;
    }

    set_socket_options(sock, 5000);

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(8080);
    if (inet_pton(AF_INET, ip_address.c_str(), &serv_addr.sin_addr) <= 0) {
        std::cerr << "Invalid address: " << WSAGetLastError() << "\n";
        closesocket(sock);
        WSACleanup();
        return 1;
    }

    if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) == SOCKET_ERROR) {
        std::cerr << "Connection failed: " << WSAGetLastError() << "\n";
        closesocket(sock);
        WSACleanup();
        return 1;
    }

    int valread = recv(sock, buffer, sizeof(buffer) - 1, 0);
    if (valread > 0) {
        buffer[valread] = '\0';
        std::cout << "Server: " << buffer << "\n";
    }
    else {
        std::cerr << "Recv failed: " << WSAGetLastError() << "\n";
        closesocket(sock);
        WSACleanup();
        return 1;
    }

    Vector2 vec2(1.5, 2.5);
    Vector3 vec3(3.0, 4.0, 5.0);
    std::string input;
    volatile bool stop_streaming = true;

    while (true) {
        std::cout << "Enter command ('2' for Vector2, '3' for Vector3, 'list' for file list, 'get' for file retrieval, 'move' to move all files, 'screen' to view server screen, 'stop' to stop screen, 'exit' to quit): ";
        std::getline(std::cin, input);
        ULONGLONG start_time = GetTickCount64();

        if (input == "exit") {
            if (!stop_streaming) {
                char type = 8;
                send(sock, &type, 1, 0);
                stop_streaming = true;
            }
            break;
        }
        else if (input == "2") {
            char type = 2;
            double data[2] = { vec2.x, vec2.y };
            std::cout << "Sending Vector2: (" << vec2.x << ", " << vec2.y << ")\n";
            clear_socket_buffer(sock);
            if (send(sock, &type, 1, 0) == SOCKET_ERROR || send(sock, (char*)data, sizeof(data), 0) == SOCKET_ERROR) {
                std::cerr << "Send failed: " << WSAGetLastError() << "\n";
                closesocket(sock);
                sock = reconnect(serv_addr, ip_address);
                if (sock == INVALID_SOCKET) continue;
            }

            char echo_type;
            double echo_data[2];
            if (recv(sock, &echo_type, 1, 0) != 1 || echo_type != 2 || recv(sock, (char*)echo_data, sizeof(echo_data), 0) != sizeof(echo_data)) {
                std::cerr << "Failed to receive Vector2 echo: " << WSAGetLastError() << "\n";
                continue;
            }
            std::cout << "Received Vector2 echo: (" << echo_data[0] << ", " << echo_data[1] << ")\n";
        }
        else if (input == "3") {
            char type = 3;
            double data[3] = { vec3.x, vec3.y, vec3.z };
            std::cout << "Sending Vector3: (" << vec3.x << ", " << vec3.y << ", " << vec3.z << ")\n";
            clear_socket_buffer(sock);
            if (send(sock, &type, 1, 0) == SOCKET_ERROR || send(sock, (char*)data, sizeof(data), 0) == SOCKET_ERROR) {
                std::cerr << "Send failed: " << WSAGetLastError() << "\n";
                closesocket(sock);
                sock = reconnect(serv_addr, ip_address);
                if (sock == INVALID_SOCKET) continue;
            }

            char echo_type;
            double echo_data[3];
            if (recv(sock, &echo_type, 1, 0) != 1 || echo_type != 3 || recv(sock, (char*)echo_data, sizeof(echo_data), 0) != sizeof(echo_data)) {
                std::cerr << "Failed to receive Vector3 echo: " << WSAGetLastError() << "\n";
                continue;
            }
            std::cout << "Received Vector3 echo: (" << echo_data[0] << ", " << echo_data[1] << ", " << echo_data[2] << ")\n";
        }
        else if (input == "list") {
            char type = 4;
            std::string path;
            std::cout << "Enter directory path to list (leave empty for current directory): ";
            std::getline(std::cin, path);
            if (path.empty()) path = " ";
            bool list_success = false;
            int retries = 3;
            while (retries > 0 && !list_success) {
                clear_socket_buffer(sock);
                if (send(sock, &type, 1, 0) == SOCKET_ERROR || send(sock, path.c_str(), path.size() + 1, 0) == SOCKET_ERROR) {
                    std::cerr << "Send failed for list command: " << WSAGetLastError() << "\n";
                    retries--;
                    if (retries == 0) {
                        std::cerr << "Failed to send list command after 3 retries\n";
                        break;
                    }
                    closesocket(sock);
                    sock = reconnect(serv_addr, ip_address);
                    if (sock == INVALID_SOCKET) {
                        std::cerr << "Reconnection failed, aborting list command\n";
                        break;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(2000));
                    continue;
                }

                char echo_type;
                if (recv(sock, &echo_type, 1, 0) != 1 || echo_type != 4) {
                    std::cerr << "Invalid response type for list command: " << WSAGetLastError() << "\n";
                    retries--;
                    if (retries == 0) {
                        std::cerr << "Failed to receive list response after 3 retries\n";
                        break;
                    }
                    closesocket(sock);
                    sock = reconnect(serv_addr, ip_address);
                    if (sock == INVALID_SOCKET) {
                        std::cerr << "Reconnection failed, aborting list command\n";
                        break;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(2000));
                    continue;
                }

                char file_buffer[16384] = { 0 };
                valread = recv(sock, file_buffer, sizeof(file_buffer) - 1, 0);
                if (valread <= 0) {
                    std::cerr << "Failed to receive file list: " << WSAGetLastError() << "\n";
                    retries--;
                    if (retries == 0) {
                        std::cerr << "Failed to receive file list after 3 retries\n";
                        break;
                    }
                    closesocket(sock);
                    sock = reconnect(serv_addr, ip_address);
                    if (sock == INVALID_SOCKET) {
                        std::cerr << "Reconnection failed, aborting list command\n";
                        break;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(2000));
                    continue;
                }
                file_buffer[valread] = '\0';
                std::cout << "Files in directory:\n" << file_buffer << "\n";
                list_success = true;
            }
            if (!list_success) continue;
        }
        else if (input == "get") {
            char type = 5;
            std::string file_path;
            std::cout << "Enter file path to retrieve: ";
            std::getline(std::cin, file_path);
            clear_socket_buffer(sock);
            if (send(sock, &type, 1, 0) == SOCKET_ERROR || send(sock, file_path.c_str(), file_path.size() + 1, 0) == SOCKET_ERROR) {
                std::cerr << "Send failed: " << WSAGetLastError() << "\n";
                closesocket(sock);
                sock = reconnect(serv_addr, ip_address);
                if (sock == INVALID_SOCKET) continue;
            }

            char echo_type;
            if (recv(sock, &echo_type, 1, 0) != 1 || echo_type != 5) {
                std::cerr << "Invalid response type: " << WSAGetLastError() << "\n";
                continue;
            }

            std::streamsize size;
            valread = recv(sock, (char*)&size, sizeof(size), 0);
            if (valread != sizeof(size)) {
                std::cerr << "Failed to receive file size: " << WSAGetLastError() << "\n";
                continue;
            }

            if (size == 0) {
                char error_buffer[1024] = { 0 };
                valread = recv(sock, error_buffer, sizeof(error_buffer) - 1, 0);
                if (valread > 0) {
                    error_buffer[valread] = '\0';
                    std::cout << "Server error: " << error_buffer << "\n";
                }
                else {
                    std::cout << "Server error: Empty error response\n";
                }
                continue;
            }

            std::vector<char> file_buffer(size);
            int total_received = 0;
            while (total_received < size) {
                valread = recv(sock, file_buffer.data() + total_received, size - total_received, 0);
                if (valread <= 0) {
                    std::cerr << "Failed to receive file data: " << WSAGetLastError() << "\n";
                    break;
                }
                total_received += valread;
            }
            if (total_received != size) {
                std::cerr << "Incomplete file data received\n";
                continue;
            }

            std::string filename = file_path.substr(file_path.find_last_of("/\\") + 1);
            std::ofstream outfile(filename, std::ios::binary);
            if (!outfile) {
                std::cerr << "Failed to save file: " << filename << "\n";
                continue;
            }
            outfile.write(file_buffer.data(), size);
            outfile.close();
            std::cout << "File saved as: " << filename << " (" << size << " bytes)\n";
        }
        else if (input == "move") {
            std::string folder_name;
            std::cout << "Enter name for new folder to store files: ";
            std::getline(std::cin, folder_name);
            if (folder_name.empty()) {
                folder_name = "ServerFiles_" + std::to_string(start_time);
            }
            try {
                if (fs::exists(folder_name)) {
                    std::cerr << "Folder already exists: " << folder_name << ". Please choose a different name.\n";
                    continue;
                }
                fs::create_directory(folder_name);
                std::cout << "Created folder: " << folder_name << "\n";
            }
            catch (const fs::filesystem_error& e) {
                std::cerr << "Failed to create folder: " << e.what() << "\n";
                continue;
            }

            std::vector<std::string> files;
            std::string base_path;
            std::cout << "Enter directory path to list (leave empty for current directory): ";
            std::getline(std::cin, base_path);
            if (base_path.empty()) base_path = " ";
            bool list_success = false;
            int retries = 3;
            while (retries > 0 && !list_success) {
                char type = 4;
                clear_socket_buffer(sock);
                if (send(sock, &type, 1, 0) == SOCKET_ERROR || send(sock, base_path.c_str(), base_path.size() + 1, 0) == SOCKET_ERROR) {
                    std::cerr << "Send failed for list command: " << WSAGetLastError() << "\n";
                    retries--;
                    if (retries == 0) {
                        std::cerr << "Failed to send list command after 3 retries\n";
                        break;
                    }
                    closesocket(sock);
                    sock = reconnect(serv_addr, ip_address);
                    if (sock == INVALID_SOCKET) {
                        std::cerr << "Reconnection failed, aborting move command\n";
                        break;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(2000));
                    continue;
                }

                char echo_type;
                if (recv(sock, &echo_type, 1, 0) != 1 || echo_type != 4) {
                    std::cerr << "Invalid response type for list command: " << WSAGetLastError() << "\n";
                    retries--;
                    if (retries == 0) {
                        std::cerr << "Failed to receive list response after 3 retries\n";
                        break;
                    }
                    closesocket(sock);
                    sock = reconnect(serv_addr, ip_address);
                    if (sock == INVALID_SOCKET) {
                        std::cerr << "Reconnection failed, aborting move command\n";
                        break;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(2000));
                    continue;
                }

                char file_buffer[16384] = { 0 };
                valread = recv(sock, file_buffer, sizeof(file_buffer) - 1, 0);
                if (valread <= 0) {
                    std::cerr << "Failed to receive file list: " << WSAGetLastError() << "\n";
                    retries--;
                    if (retries == 0) {
                        std::cerr << "Failed to receive file list after 3 retries\n";
                        break;
                    }
                    closesocket(sock);
                    sock = reconnect(serv_addr, ip_address);
                    if (sock == INVALID_SOCKET) {
                        std::cerr << "Reconnection failed, aborting move command\n";
                        break;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(2000));
                    continue;
                }
                file_buffer[valread] = '\0';

                std::string file_list = file_buffer;
                if (file_list.empty() || file_list == "Directory is empty or contains only 0KB files" || file_list.find("Error:") == 0) {
                    std::cout << "No non-empty files to move: " << file_list << "\n";
                    list_success = true;
                    break;
                }

                std::string file;
                for (char c : file_list) {
                    if (c == '\n' && !file.empty()) {
                        files.push_back(file);
                        file.clear();
                    }
                    else if (c != '\n') file += c;
                }
                if (!file.empty()) files.push_back(file);
                list_success = true;
            }
            if (!list_success) continue;

            int successful_transfers = 0;
            int total_files = files.size();
            for (size_t i = 0; i < files.size(); ++i) {
                const auto& file_path = files[i];
                std::cout << "[" << (i + 1) << "/" << total_files << "] Attempting to retrieve: " << file_path << "\n";
                bool success = false;
                int file_retries = 3;
                while (file_retries > 0 && !success) {
                    clear_socket_buffer(sock);
                    char type = 5;
                    if (send(sock, &type, 1, 0) == SOCKET_ERROR || send(sock, file_path.c_str(), file_path.size() + 1, 0) == SOCKET_ERROR) {
                        std::cerr << "Send failed for file: " << file_path << " (" << WSAGetLastError() << ")\n";
                        file_retries--;
                        if (file_retries == 0) {
                            std::cerr << "Failed to send file request for " << file_path << " after 3 retries\n";
                            break;
                        }
                        closesocket(sock);
                        sock = reconnect(serv_addr, ip_address);
                        if (sock == INVALID_SOCKET) {
                            std::cerr << "Reconnection failed, skipping file: " << file_path << "\n";
                            break;
                        }
                        std::this_thread::sleep_for(std::chrono::milliseconds(2000));
                        continue;
                    }

                    char echo_type;
                    valread = recv(sock, &echo_type, 1, 0);
                    if (valread != 1 || echo_type != 5) {
                        std::cerr << "Invalid response type for file: " << file_path << " (" << WSAGetLastError() << ")\n";
                        file_retries--;
                        if (file_retries == 0) {
                            std::cerr << "Failed to receive file response for " << file_path << " after 3 retries\n";
                            break;
                        }
                        closesocket(sock);
                        sock = reconnect(serv_addr, ip_address);
                        if (sock == INVALID_SOCKET) {
                            std::cerr << "Reconnection failed, skipping file: " << file_path << "\n";
                            break;
                        }
                        std::this_thread::sleep_for(std::chrono::milliseconds(2000));
                        continue;
                    }

                    std::streamsize size;
                    valread = recv(sock, (char*)&size, sizeof(size), 0);
                    if (valread != sizeof(size)) {
                        std::cerr << "Failed to receive file size for: " << file_path << " (" << WSAGetLastError() << ")\n";
                        file_retries--;
                        if (file_retries == 0) {
                            std::cerr << "Failed to receive file size for " << file_path << " after 3 retries\n";
                            break;
                        }
                        closesocket(sock);
                        sock = reconnect(serv_addr, ip_address);
                        if (sock == INVALID_SOCKET) {
                            std::cerr << "Reconnection failed, skipping file: " << file_path << "\n";
                            break;
                        }
                        std::this_thread::sleep_for(std::chrono::milliseconds(2000));
                        continue;
                    }

                    if (size == 0) {
                        char error_buffer[1024] = { 0 };
                        valread = recv(sock, error_buffer, sizeof(error_buffer) - 1, 0);
                        if (valread > 0) {
                            error_buffer[valread] = '\0';
                            std::cout << "Server error for " << file_path << ": " << error_buffer << "\n";
                        }
                        else {
                            std::cout << "Server error for " << file_path << ": Empty error response\n";
                        }
                        file_retries--;
                        if (file_retries == 0) {
                            std::cerr << "Failed to retrieve file " << file_path << " after 3 retries\n";
                            break;
                        }
                        closesocket(sock);
                        sock = reconnect(serv_addr, ip_address);
                        if (sock == INVALID_SOCKET) {
                            std::cerr << "Reconnection failed, skipping file: " << file_path << "\n";
                            break;
                        }
                        std::this_thread::sleep_for(std::chrono::milliseconds(2000));
                        continue;
                    }

                    if (size > 1024 * 1024 * 500) {
                        std::cerr << "File too large: " << file_path << " (" << size << " bytes)\n";
                        file_retries--;
                        if (file_retries == 0) {
                            std::cerr << "File " << file_path << " too large after 3 retries\n";
                            break;
                        }
                        std::this_thread::sleep_for(std::chrono::milliseconds(2000));
                        continue;
                    }

                    std::vector<char> file_buffer(size);
                    int total_received = 0;
                    while (total_received < size) {
                        valread = recv(sock, file_buffer.data() + total_received, size - total_received, 0);
                        if (valread <= 0) {
                            std::cerr << "Failed to receive file data for: " << file_path << " (" << WSAGetLastError() << ")\n";
                            break;
                        }
                        total_received += valread;
                    }
                    if (total_received != size) {
                        std::cerr << "Incomplete file data received for: " << file_path << "\n";
                        file_retries--;
                        if (file_retries == 0) {
                            std::cerr << "Failed to receive file data for " << file_path << " after 3 retries\n";
                            break;
                        }
                        closesocket(sock);
                        sock = reconnect(serv_addr, ip_address);
                        if (sock == INVALID_SOCKET) {
                            std::cerr << "Reconnection failed, skipping file: " << file_path << "\n";
                            break;
                        }
                        std::this_thread::sleep_for(std::chrono::milliseconds(2000));
                        continue;
                    }

                    std::string relative_path = file_path;
                    if (base_path != " ") {
                        size_t pos = file_path.find(base_path);
                        if (pos != std::string::npos) {
                            relative_path = file_path.substr(pos + base_path.size());
                            if (relative_path[0] == '\\') relative_path = relative_path.substr(1);
                        }
                    }
                    std::string save_path = folder_name + "\\" + relative_path;
                    std::string save_dir = save_path.substr(0, save_path.find_last_of("\\"));
                    if (!save_dir.empty()) {
                        try {
                            fs::create_directories(save_dir);
                        }
                        catch (const fs::filesystem_error& e) {
                            std::cerr << "Failed to create directory " << save_dir << ": " << e.what() << "\n";
                            file_retries--;
                            if (file_retries == 0) {
                                std::cerr << "Failed to create directory for " << file_path << " after 3 retries\n";
                                break;
                            }
                            continue;
                        }
                    }

                    std::ofstream outfile(save_path, std::ios::binary);
                    if (!outfile) {
                        std::cerr << "Failed to save file: " << save_path << "\n";
                        file_retries--;
                        if (file_retries == 0) {
                            std::cerr << "Failed to save file " << file_path << " after 3 retries\n";
                            break;
                        }
                        std::this_thread::sleep_for(std::chrono::milliseconds(2000));
                        continue;
                    }
                    outfile.write(file_buffer.data(), size);
                    outfile.close();
                    std::cout << "File saved as: " << save_path << " (" << size << " bytes)\n";
                    success = true;
                    successful_transfers++;
                    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
                }
                if (!success) {
                    std::cerr << "Failed to transfer " << file_path << " after 3 retries\n";
                }
            }
            std::cout << "Moved " << successful_transfers << " of " << total_files << " files to folder: " << folder_name << "\n";
        }
        else if (input == "screen") {
            if (!stop_streaming) {
                std::cout << "Screen streaming already active. Use 'stop' to end it.\n";
                continue;
            }
            int target_width = 640, target_height = 480;
            std::cout << "Enter target width (default 640): ";
            std::string width_input;
            std::getline(std::cin, width_input);
            if (!width_input.empty()) {
                try { target_width = std::stoi(width_input); }
                catch (...) {}
            }
            std::cout << "Enter target height (default 480): ";
            std::string height_input;
            std::getline(std::cin, height_input);
            if (!height_input.empty()) {
                try { target_height = std::stoi(height_input); }
                catch (...) {}
            }
            if (target_width < 320 || target_height < 240) {
                std::cerr << "Resolution too small, using default 640x480\n";
                target_width = 640;
                target_height = 480;
            }
            std::cout << "Warning: Streaming at " << target_width << "x" << target_height << " at 10 FPS requires ~6-10MB/s bandwidth.\n";

            char type = 7;
            int resolution[2] = { target_width, target_height };
            clear_socket_buffer(sock);
            int retries = 3;
            bool sent = false;
            while (retries > 0 && !sent) {
                if (send(sock, &type, 1, 0) == SOCKET_ERROR || send(sock, (char*)resolution, sizeof(resolution), 0) == SOCKET_ERROR) {
                    std::cerr << "Send failed for screen command: " << WSAGetLastError() << " (retry " << (4 - retries) << "/3)\n";
                    retries--;
                    if (retries == 0) {
                        std::cerr << "Failed to send screen command after 3 retries\n";
                        break;
                    }
                    closesocket(sock);
                    sock = reconnect(serv_addr, ip_address);
                    if (sock == INVALID_SOCKET) {
                        std::cerr << "Reconnection failed, aborting screen command\n";
                        break;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(2000));
                    continue;
                }
                sent = true;
            }
            if (!sent) continue;

            stop_streaming = false;
            std::cout << "Started screen streaming. Use 'stop' to end.\n";
            auto start_time = std::chrono::steady_clock::now();
            std::thread(receive_screen_stream, std::ref(sock), std::ref(stop_streaming), target_width, target_height, ip_address, std::ref(serv_addr)).detach();
            auto end_time = std::chrono::steady_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
            std::cout << "Round-trip delay: " << duration << " ms\n";
        }
        else if (input == "stop") {
            if (stop_streaming) {
                std::cout << "No screen streaming active.\n";
                continue;
            }
            char type = 8;
            clear_socket_buffer(sock);
            if (send(sock, &type, 1, 0) == SOCKET_ERROR) {
                std::cerr << "Send failed for stop command: " << WSAGetLastError() << "\n";
                closesocket(sock);
                sock = reconnect(serv_addr, ip_address);
                if (sock == INVALID_SOCKET) continue;
            }
            stop_streaming = true;
            std::cout << "Stopped screen streaming\n";
        }
        else {
            std::cout << "Invalid command\n";
        }
    }

    closesocket(sock);
    WSACleanup();
    return 0;
}
