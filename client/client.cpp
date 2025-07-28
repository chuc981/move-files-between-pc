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

// Clear socket buffer to prevent residual data
void clear_socket_buffer(SOCKET sock) {
    char temp_buffer[1024];
    DWORD timeout = 100; // 100ms timeout for clearing
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
    while (recv(sock, temp_buffer, sizeof(temp_buffer), 0) > 0) {}
    set_socket_timeout(sock, 60000); // Restore to 60 seconds
}

// Reconnect to server if connection is lost
SOCKET reconnect(struct sockaddr_in& serv_addr, const std::string& ip_address) {
    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        std::cerr << "Socket creation error: " << WSAGetLastError() << "\n";
        return INVALID_SOCKET;
    }
    set_socket_timeout(sock, 60000);
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

int main() {
    WSADATA wsaData;
    SOCKET sock = INVALID_SOCKET;
    struct sockaddr_in serv_addr;
    char buffer[16384] = { 0 }; // Increased buffer size
    std::string ip_address;

    // Initialize Winsock
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed: " << WSAGetLastError() << "\n";
        return 1;
    }

    // Get server IP from user
    std::cout << "Enter server IP address: ";
    std::getline(std::cin, ip_address);

    // Create socket
    sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        std::cerr << "Socket creation error: " << WSAGetLastError() << "\n";
        WSACleanup();
        return 1;
    }

    // Set socket timeouts (60 seconds)
    set_socket_timeout(sock, 60000);

    // Configure server address
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(8080);
    if (inet_pton(AF_INET, ip_address.c_str(), &serv_addr.sin_addr) <= 0) {
        std::cerr << "Invalid address: " << WSAGetLastError() << "\n";
        closesocket(sock);
        WSACleanup();
        return 1;
    }

    // Connect to server
    if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) == SOCKET_ERROR) {
        std::cerr << "Connection failed: " << WSAGetLastError() << "\n";
        closesocket(sock);
        WSACleanup();
        return 1;
    }

    // Receive connection acknowledgment
    int valread = recv(sock, buffer, sizeof(buffer) - 1, 0);
    if (valread > 0) {
        buffer[valread] = '\0';
        std::cout << "Server: " << buffer << "\n";
    }
    else if (valread == SOCKET_ERROR) {
        std::cerr << "Recv failed: " << WSAGetLastError() << "\n";
        closesocket(sock);
        WSACleanup();
        return 1;
    }

    // Test vectors
    Vector2 vec2(1.5, 2.5);
    Vector3 vec3(3.0, 4.0, 5.0);
    std::string input;

    while (true) {
        std::cout << "Enter command ('2' for Vector2, '3' for Vector3, 'list' for file list, 'get' for file retrieval, 'move' to move all files to new folder, 'exit' to quit): ";
        std::getline(std::cin, input);
        ULONGLONG start_time = GetTickCount64();

        if (input == "exit") {
            break;
        }
        else if (input == "2") {
            char type = 2;
            double data[2] = { vec2.x, vec2.y };
            std::cout << "Sending Vector2: (" << vec2.x << ", " << vec2.y << ")\n";
            clear_socket_buffer(sock);
            if (send(sock, &type, 1, 0) == SOCKET_ERROR || send(sock, (char*)data, sizeof(data), 0) == SOCKET_ERROR) {
                std::cerr << "Send failed: " << WSAGetLastError() << "\n";
                continue;
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
                continue;
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
            if (path.empty()) {
                path = " ";
            }
            bool list_success = false;
            int retries = 3;
            while (retries > 0 && !list_success) {
                if (send(sock, &type, 1, 0) == SOCKET_ERROR || send(sock, path.c_str(), path.size() + 1, 0) == SOCKET_ERROR) {
                    std::cerr << "Send failed for list command: " << WSAGetLastError() << "\n";
                    retries--;
                    if (retries == 0) {
                        std::cerr << "Failed to send list command after 3 retries\n";
                        break;
                    }
                    // Attempt to reconnect
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
                    // Attempt to reconnect
                    closesocket(sock);
                    sock = reconnect(serv_addr, ip_address);
                    if (sock == INVALID_SOCKET) {
                        std::cerr << "Reconnection failed, aborting list command\n";
                        break;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(2000));
                    continue;
                }

                char file_buffer[16384] = { 0 }; // Increased buffer size
                valread = recv(sock, file_buffer, sizeof(file_buffer) - 1, 0);
                if (valread <= 0) {
                    std::cerr << "Failed to receive file list: " << WSAGetLastError() << "\n";
                    retries--;
                    if (retries == 0) {
                        std::cerr << "Failed to receive file list after 3 retries\n";
                        break;
                    }
                    // Attempt to reconnect
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
            if (!list_success) {
                continue;
            }
        }
        else if (input == "get") {
            char type = 5;
            std::string file_path;
            std::cout << "Enter file path to retrieve: ";
            std::getline(std::cin, file_path);
            clear_socket_buffer(sock);
            if (send(sock, &type, 1, 0) == SOCKET_ERROR || send(sock, file_path.c_str(), file_path.size() + 1, 0) == SOCKET_ERROR) {
                std::cerr << "Send failed: " << WSAGetLastError() << "\n";
                continue;
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
            // Create folder first
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

            // Get file list with retries
            std::vector<std::string> files;
            std::string base_path;
            std::cout << "Enter directory path to list (leave empty for current directory): ";
            std::getline(std::cin, base_path);
            if (base_path.empty()) {
                base_path = " ";
            }
            bool list_success = false;
            int retries = 3;
            while (retries > 0 && !list_success) {
                char type = 4;
                if (send(sock, &type, 1, 0) == SOCKET_ERROR || send(sock, base_path.c_str(), base_path.size() + 1, 0) == SOCKET_ERROR) {
                    std::cerr << "Send failed for list command: " << WSAGetLastError() << "\n";
                    retries--;
                    if (retries == 0) {
                        std::cerr << "Failed to send list command after 3 retries\n";
                        break;
                    }
                    // Attempt to reconnect
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
                    // Attempt to reconnect
                    closesocket(sock);
                    sock = reconnect(serv_addr, ip_address);
                    if (sock == INVALID_SOCKET) {
                        std::cerr << "Reconnection failed, aborting move command\n";
                        break;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(2000));
                    continue;
                }

                char file_buffer[16384] = { 0 }; // Increased buffer size
                valread = recv(sock, file_buffer, sizeof(file_buffer) - 1, 0);
                if (valread <= 0) {
                    std::cerr << "Failed to receive file list: " << WSAGetLastError() << "\n";
                    retries--;
                    if (retries == 0) {
                        std::cerr << "Failed to receive file list after 3 retries\n";
                        break;
                    }
                    // Attempt to reconnect
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

                // Parse file list
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
                    else if (c != '\n') {
                        file += c;
                    }
                }
                if (!file.empty()) {
                    files.push_back(file);
                }
                list_success = true;
            }
            if (!list_success) {
                continue;
            }

            // Retrieve and save each file
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
                        // Attempt to reconnect
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
                        // Attempt to reconnect
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
                        // Attempt to reconnect
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
                        // Attempt to reconnect
                        closesocket(sock);
                        sock = reconnect(serv_addr, ip_address);
                        if (sock == INVALID_SOCKET) {
                            std::cerr << "Reconnection failed, skipping file: " << file_path << "\n";
                            break;
                        }
                        std::this_thread::sleep_for(std::chrono::milliseconds(2000));
                        continue;
                    }

                    // Validate file size
                    if (size > 1024 * 1024 * 500) { // Limit to 500MB
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
                        // Attempt to reconnect
                        closesocket(sock);
                        sock = reconnect(serv_addr, ip_address);
                        if (sock == INVALID_SOCKET) {
                            std::cerr << "Reconnection failed, skipping file: " << file_path << "\n";
                            break;
                        }
                        std::this_thread::sleep_for(std::chrono::milliseconds(2000));
                        continue;
                    }

                    // Create directory structure relative to base_path
                    std::string relative_path = file_path;
                    if (base_path != " ") {
                        size_t pos = file_path.find(base_path);
                        if (pos != std::string::npos) {
                            relative_path = file_path.substr(pos + base_path.size());
                            if (relative_path[0] == '\\') {
                                relative_path = relative_path.substr(1);
                            }
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
        else {
            std::cout << "Invalid input. Use '2', '3', 'list', 'get', 'move', or 'exit'.\n";
            continue;
        }

        ULONGLONG end_time = GetTickCount64();
        std::cout << "Round-trip delay: " << (end_time - start_time) << " ms\n";
    }

    closesocket(sock);
    WSACleanup();
    return 0;
}
