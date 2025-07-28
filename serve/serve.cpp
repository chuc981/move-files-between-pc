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
namespace fs = std::filesystem;

#pragma comment(lib, "Ws2_32.lib")

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

// Set socket timeout for send and receive operations
void set_socket_timeout(SOCKET sock, int timeout_ms) {
    DWORD timeout = timeout_ms;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout));
}

// Recursively list files in a directory, skipping 0KB files
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

void handle_client(SOCKET client_socket) {
    set_socket_timeout(client_socket, 60000); // 60-second timeout
    char buffer[8192] = { 0 }; // Increased buffer size
    while (true) {
        // Receive type identifier
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

            // If path is empty, use current working directory
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

            // Check file validity
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

            // Send file data
            if (send(client_socket, &type, 1, 0) == SOCKET_ERROR ||
                send(client_socket, (char*)&size, sizeof(size), 0) == SOCKET_ERROR ||
                send(client_socket, file_buffer.data(), size, 0) == SOCKET_ERROR) {
                std::cerr << "Send failed for file: " << file_path << " (" << WSAGetLastError() << ")\n";
                break;
            }
            std::cout << "Sent file: " << file_path << " (" << size << " bytes)\n";
        }
        else {
            std::cerr << "Unknown type received: " << (int)type << "\n";
            continue;
        }
    }
    closesocket(client_socket);
}

int main() {
    WSADATA wsaData;
    SOCKET server_fd;
    struct sockaddr_in server_addr;
    char hostname[256];
    char ip_buffer[INET_ADDRSTRLEN];

    // Initialize Winsock
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed: " << WSAGetLastError() << "\n";
        return 1;
    }

    // Get hostname and IP
    gethostname(hostname, sizeof(hostname));
    struct hostent* host = gethostbyname(hostname);
    if (host == nullptr) {
        strcpy(ip_buffer, "127.0.0.1");
    }
    else {
        strcpy(ip_buffer, inet_ntoa(*(struct in_addr*)host->h_addr_list[0]));
    }

    // Create socket
    server_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_fd == INVALID_SOCKET) {
        std::cerr << "Socket creation failed: " << WSAGetLastError() << "\n";
        WSACleanup();
        return 1;
    }

    // Enable reuse address
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

    // Configure server address
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(8080);

    // Bind socket
    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        std::cerr << "Bind failed: " << WSAGetLastError() << "\n";
        closesocket(server_fd);
        WSACleanup();
        return 1;
    }

    // Listen for connections
    if (listen(server_fd, 3) == SOCKET_ERROR) {
        std::cerr << "Listen failed: " << WSAGetLastError() << "\n";
        closesocket(server_fd);
        WSACleanup();
        return 1;
    }

    std::cout << "Server running. Connect client to IP: " << ip_buffer << ", Port: 8080\n";

    while (true) {
        // Accept client connection
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
        }

        // Handle client in a separate thread
        std::thread(handle_client, new_socket).detach();
    }

    closesocket(server_fd);
    WSACleanup();
    return 0;
}
