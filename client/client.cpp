#define _CRT_SECURE_NO_WARNINGS
#define _WINSOCK_DEPRECATED_NO_WARNINGS 
#include <iostream>
#include <string>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <fstream>
#include <filesystem>
#include <thread>
#include <sstream>
#include <vector>
#include <windows.h>
#include <chrono>
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

// Recursively list files, skipping 0KB files
void list_files_recursive(const std::string& path, std::string& file_list) {
    try {
        for (const auto& entry : fs::recursive_directory_iterator(path)) {
            if (entry.is_regular_file() && fs::file_size(entry.path()) > 0) {
                file_list += entry.path().string() + "\n";
            }
            else if (entry.is_regular_file()) {
                std::cout << "Skipped 0KB file: " << entry.path().string() << "\n";
            }
        }
    }
    catch (const fs::filesystem_error& e) {
        file_list += "Error accessing directory: " + std::string(e.what()) + "\n";
    }
}

// Simple RLE compression for RGB data
bool compress_rle(const unsigned char* input, int input_size, std::vector<unsigned char>& output) {
    output.clear();
    output.reserve(input_size / 2);
    int i = 0;
    while (i < input_size) {
        if (i + 2 >= input_size) {
            std::cerr << "Incomplete pixel data at index " << i << "\n";
            return false;
        }
        unsigned char pixel[3] = { input[i], input[i + 1], input[i + 2] };
        int count = 1;
        i += 3;
        while (i + 2 < input_size && count < 255 &&
            input[i] == pixel[0] && input[i + 1] == pixel[1] && input[i + 2] == pixel[2]) {
            count++;
            i += 3;
        }
        output.push_back((unsigned char)count);
        output.push_back(pixel[0]);
        output.push_back(pixel[1]);
        output.push_back(pixel[2]);
    }
    std::cout << "RLE compressed " << input_size << " bytes to " << output.size() << " bytes\n";
    return true;
}

// Capture screen
bool capture_screen(std::vector<unsigned char>& data, int& width, int& height, int target_width, int target_height) {
    HDC hScreenDC = GetDC(NULL);
    if (!hScreenDC) {
        std::cerr << "Failed to get screen DC: " << GetLastError() << "\n";
        return false;
    }
    HDC hMemoryDC = CreateCompatibleDC(hScreenDC);
    if (!hMemoryDC) {
        std::cerr << "Failed to create memory DC: " << GetLastError() << "\n";
        ReleaseDC(NULL, hScreenDC);
        return false;
    }
    int screen_width = GetSystemMetrics(SM_CXSCREEN);
    int screen_height = GetSystemMetrics(SM_CYSCREEN);
    width = target_width;
    height = target_height;
    HBITMAP hBitmap = CreateCompatibleBitmap(hScreenDC, width, height);
    if (!hBitmap) {
        std::cerr << "Failed to create bitmap: " << GetLastError() << "\n";
        DeleteDC(hMemoryDC);
        ReleaseDC(NULL, hScreenDC);
        return false;
    }
    HBITMAP hOldBitmap = (HBITMAP)SelectObject(hMemoryDC, hBitmap);

    SetStretchBltMode(hMemoryDC, HALFTONE);
    if (!StretchBlt(hMemoryDC, 0, 0, width, height, hScreenDC, 0, 0, screen_width, screen_height, SRCCOPY)) {
        std::cerr << "StretchBlt failed: " << GetLastError() << "\n";
        SelectObject(hMemoryDC, hOldBitmap);
        DeleteObject(hBitmap);
        DeleteDC(hMemoryDC);
        ReleaseDC(NULL, hScreenDC);
        return false;
    }

    BITMAPINFO bmi = { 0 };
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 24;
    bmi.bmiHeader.biCompression = BI_RGB;

    std::vector<unsigned char> bmp_data(width * height * 3);
    if (!GetDIBits(hMemoryDC, hBitmap, 0, height, bmp_data.data(), &bmi, DIB_RGB_COLORS)) {
        std::cerr << "GetDIBits failed: " << GetLastError() << "\n";
        SelectObject(hMemoryDC, hOldBitmap);
        DeleteObject(hBitmap);
        DeleteDC(hMemoryDC);
        ReleaseDC(NULL, hScreenDC);
        return false;
    }

    if (!compress_rle(bmp_data.data(), bmp_data.size(), data)) {
        std::cerr << "RLE compression failed\n";
        SelectObject(hMemoryDC, hOldBitmap);
        DeleteObject(hBitmap);
        DeleteDC(hMemoryDC);
        ReleaseDC(NULL, hScreenDC);
        return false;
    }

    SelectObject(hMemoryDC, hOldBitmap);
    DeleteObject(hBitmap);
    DeleteDC(hMemoryDC);
    ReleaseDC(NULL, hScreenDC);
    return true;
}

void stream_screen(SOCKET& client_socket, volatile bool& stop_streaming, int target_width, int target_height) {
    set_socket_options(client_socket, 5000); // 5s timeout
    int FPS = 10; // Start at 10 FPS
    int frame_interval_ms = 1000 / FPS;
    int frame_count = 0;
    auto stream_start = std::chrono::steady_clock::now();

    while (!stop_streaming) {
        if (!is_socket_valid(client_socket)) {
            std::cerr << "Socket invalid in stream_screen, stopping\n";
            stop_streaming = true;
            break;
        }

        auto start_time = std::chrono::steady_clock::now();
        std::vector<unsigned char> data;
        int width, height;
        if (!capture_screen(data, width, height, target_width, target_height)) {
            std::cerr << "Failed to capture screen\n";
            std::this_thread::sleep_for(std::chrono::milliseconds(frame_interval_ms));
            continue;
        }

        std::streamsize size = data.size();
        char type = 7;
        int header[3] = { width, height, (int)size };

        int retries = 3;
        bool sent = false;
        while (retries > 0 && !sent && !stop_streaming) {
            if (!is_socket_valid(client_socket)) {
                std::cerr << "Socket became invalid during send attempt for frame " << frame_count << "\n";
                stop_streaming = true;
                break;
            }
            if (send(client_socket, &type, 1, 0) == SOCKET_ERROR ||
                send(client_socket, (char*)header, sizeof(header), 0) == SOCKET_ERROR ||
                send(client_socket, (char*)data.data(), size, 0) == SOCKET_ERROR) {
                std::cerr << "Send failed for frame " << frame_count << ": " << WSAGetLastError() << " (retry " << (4 - retries) << "/3)\n";
                retries--;
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            else {
                sent = true;
                std::cout << "Sent frame " << frame_count << ": " << size << " bytes (" << width << "x" << height << ")\n";
            }
        }
        if (!sent) {
            std::cerr << "Failed to send frame " << frame_count << " after 3 retries\n";
            stop_streaming = true;
            break;
        }
        frame_count++;

        auto elapsed_total = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - stream_start).count();
        if (elapsed_total > 5 && frame_count / elapsed_total < 8) {
            FPS = max(5, FPS - 1);
            frame_interval_ms = 1000 / FPS;
            std::cout << "Reduced FPS to " << FPS << " due to low performance\n";
        }

        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start_time).count();
        int sleep_time = frame_interval_ms - elapsed;
        if (sleep_time > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(sleep_time));
        }
    }

    std::cout << "Stream screen thread exiting\n";
}

void handle_client(SOCKET client_socket) {
    set_socket_options(client_socket, 5000); // 5s timeout
    char buffer[8192] = { 0 };
    volatile bool stop_streaming = false;
    int target_width = 640, target_height = 480; // Default resolution

    while (!stop_streaming) {
        if (!is_socket_valid(client_socket)) {
            std::cerr << "Client socket invalid, closing connection\n";
            break;
        }

        char type;
        int valread = recv(client_socket, &type, 1, 0);
        if (valread == SOCKET_ERROR) {
            std::cerr << "Recv type failed: " << WSAGetLastError() << "\n";
            break;
        }
        if (valread == 0) {
            std::cout << "Client disconnected cleanly\n";
            break;
        }
        std::cout << "Received command type: " << (int)type << "\n";

        if (type == 2) { // Vector2
            double data[2];
            valread = recv(client_socket, (char*)data, sizeof(data), 0);
            if (valread != sizeof(data)) {
                std::cerr << "Incomplete Vector2 data received: " << WSAGetLastError() << "\n";
                continue;
            }
            Vector2 vec(data[0], data[1]);
            std::cout << "Received Vector2: (" << vec.x << ", " << vec.y << ")\n";
            if (send(client_socket, &type, 1, 0) == SOCKET_ERROR || send(client_socket, (char*)data, sizeof(data), 0) == SOCKET_ERROR) {
                std::cerr << "Send failed for Vector2: " << WSAGetLastError() << "\n";
                break;
            }
        }
        else if (type == 3) { // Vector3
            double data[3];
            valread = recv(client_socket, (char*)data, sizeof(data), 0);
            if (valread != sizeof(data)) {
                std::cerr << "Incomplete Vector3 data received: " << WSAGetLastError() << "\n";
                continue;
            }
            Vector3 vec(data[0], data[1], data[2]);
            std::cout << "Received Vector3: (" << vec.x << ", " << vec.y << ", " << vec.z << ")\n";
            if (send(client_socket, &type, 1, 0) == SOCKET_ERROR || send(client_socket, (char*)data, sizeof(data), 0) == SOCKET_ERROR) {
                std::cerr << "Send failed for Vector3: " << WSAGetLastError() << "\n";
                break;
            }
        }
        else if (type == 4) { // List files
            char path_buffer[1024];
            valread = recv(client_socket, path_buffer, sizeof(path_buffer) - 1, 0);
            if (valread <= 0) {
                std::cerr << "Failed to receive path: " << WSAGetLastError() << "\n";
                continue;
            }
            path_buffer[valread] = '\0';
            std::string path = path_buffer;
            std::cout << "Listing directory: " << path << "\n";

            if (path.empty() || path == " ") {
                path = fs::current_path().string();
            }

            std::string file_list;
            try {
                if (!fs::exists(path) || !fs::is_directory(path)) {
                    file_list = "Error: Invalid or non-existent directory";
                }
                else {
                    list_files_recursive(path, file_list);
                    if (file_list.empty()) {
                        file_list = "Directory is empty or contains only 0KB files";
                    }
                }
            }
            catch (const fs::filesystem_error& e) {
                file_list = "Error accessing directory: " + std::string(e.what());
            }

            if (send(client_socket, &type, 1, 0) == SOCKET_ERROR || send(client_socket, file_list.c_str(), file_list.size() + 1, 0) == SOCKET_ERROR) {
                std::cerr << "Send failed for file list: " << WSAGetLastError() << "\n";
                break;
            }
            std::cout << "Sent file list with " << std::count(file_list.begin(), file_list.end(), '\n') << " files\n";
        }
        else if (type == 5) { // Retrieve file
            char path_buffer[1024];
            valread = recv(client_socket, path_buffer, sizeof(path_buffer) - 1, 0);
            if (valread <= 0) {
                std::cerr << "Failed to receive file path: " << WSAGetLastError() << "\n";
                continue;
            }
            path_buffer[valread] = '\0';
            std::string file_path = path_buffer;
            std::cout << "Requested file: " << file_path << "\n";

            std::string error;
            std::streamsize size = 0;
            std::vector<char> file_buffer;
            bool file_valid = true;

            try {
                if (!fs::exists(file_path)) {
                    error = "Error: File does not exist: " + file_path;
                    file_valid = false;
                }
                else if (fs::is_directory(file_path)) {
                    error = "Error: Path is a directory: " + file_path;
                    file_valid = false;
                }
                else if (!fs::is_regular_file(file_path)) {
                    error = "Error: Path is not a regular file: " + file_path;
                    file_valid = false;
                }
                else if (fs::file_size(file_path) == 0) {
                    error = "Error: File is empty (0KB): " + file_path;
                    file_valid = false;
                }
                else {
                    std::ifstream file(file_path, std::ios::binary | std::ios::ate);
                    if (!file.is_open()) {
                        error = "Error: Cannot open file (permissions or locked): " + file_path;
                        file_valid = false;
                    }
                    else {
                        size = file.tellg();
                        std::cout << "File size: " << size << " bytes\n";
                        file.seekg(0, std::ios::beg);
                        file_buffer.resize(size);
                        file.read(file_buffer.data(), size);
                        file.close();
                    }
                }
            }
            catch (const fs::filesystem_error& e) {
                error = "Error: Filesystem error: " + std::string(e.what());
                file_valid = false;
            }

            if (!file_valid) {
                if (send(client_socket, &type, 1, 0) == SOCKET_ERROR ||
                    send(client_socket, (char*)&size, sizeof(size), 0) == SOCKET_ERROR ||
                    send(client_socket, error.c_str(), error.size() + 1, 0) == SOCKET_ERROR) {
                    std::cerr << "Send failed for file error: " << WSAGetLastError() << "\n";
                    break;
                }
                std::cout << "Sent error: " << error << "\n";
                continue;
            }

            if (send(client_socket, &type, 1, 0) == SOCKET_ERROR ||
                send(client_socket, (char*)&size, sizeof(size), 0) == SOCKET_ERROR ||
                send(client_socket, file_buffer.data(), size, 0) == SOCKET_ERROR) {
                std::cerr << "Send failed for file: " << file_path << " (" << WSAGetLastError() << ")\n";
                break;
            }
            std::cout << "Sent file: " << file_path << " (" << size << " bytes)\n";
        }
        else if (type == 7) { // Start screen streaming
            int resolution[2];
            valread = recv(client_socket, (char*)resolution, sizeof(resolution), 0);
            if (valread != sizeof(resolution)) {
                std::cerr << "Failed to receive resolution: " << WSAGetLastError() << "\n";
                continue;
            }
            target_width = resolution[0];
            target_height = resolution[1];
            std::cout << "Streaming at resolution: " << target_width << "x" << target_height << "\n";
            stop_streaming = false;
            std::cout << "Starting screen streaming thread\n";
            std::thread(stream_screen, std::ref(client_socket), std::ref(stop_streaming), target_width, target_height).detach();
            std::cout << "Started screen streaming\n";
        }
        else if (type == 8) { // Stop screen streaming
            stop_streaming = true;
            std::cout << "Received stop command\n";
            if (send(client_socket, &type, 1, 0) == SOCKET_ERROR) {
                std::cerr << "Send failed for stop streaming: " << WSAGetLastError() << "\n";
                break;
            }
            std::cout << "Stopped screen streaming\n";
        }
        else {
            std::cerr << "Unknown type received: " << (int)type << "\n";
            continue;
        }
    }

    stop_streaming = true;
    if (is_socket_valid(client_socket)) {
        closesocket(client_socket);
    }
}

int main() {
    WSADATA wsaData;
    SOCKET server_fd;
    struct sockaddr_in server_addr;
    char hostname[256];
    char ip_buffer[INET_ADDRSTRLEN];

    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed: " << WSAGetLastError() << "\n";
        return 1;
    }

    gethostname(hostname, sizeof(hostname));
    struct hostent* host = gethostbyname(hostname);
    if (host == nullptr) {
        strcpy(ip_buffer, "127.0.0.1");
    }
    else {
        strcpy(ip_buffer, inet_ntoa(*(struct in_addr*)host->h_addr_list[0]));
    }

    server_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_fd == INVALID_SOCKET) {
        std::cerr << "Socket creation failed: " << WSAGetLastError() << "\n";
        WSACleanup();
        return 1;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
    set_socket_options(server_fd, 5000);

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(8080);

    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        std::cerr << "Bind failed: " << WSAGetLastError() << "\n";
        closesocket(server_fd);
        WSACleanup();
        return 1;
    }

    if (listen(server_fd, 3) == SOCKET_ERROR) {
        std::cerr << "Listen failed: " << WSAGetLastError() << "\n";
        closesocket(server_fd);
        WSACleanup();
        return 1;
    }

    std::cout << "Server running. Connect client to IP: " << ip_buffer << ", Port: 8080\n";

    while (true) {
        struct sockaddr_in client_addr;
        int addr_len = sizeof(client_addr);
        SOCKET new_socket = accept(server_fd, (struct sockaddr*)&client_addr, &addr_len);
        if (new_socket == INVALID_SOCKET) {
            std::cerr << "Accept failed: " << WSAGetLastError() << "\n";
            continue;
        }

        std::cout << "Client connected\n";
        if (send(new_socket, "Connected to server\n", 20, 0) == SOCKET_ERROR) {
            std::cerr << "Send failed: " << WSAGetLastError() << "\n";
            closesocket(new_socket);
            continue;
        }

        std::thread(handle_client, new_socket).detach();
    }

    closesocket(server_fd);
    WSACleanup();
    return 0;
}
