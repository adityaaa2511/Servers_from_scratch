#include <iostream>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include <vector>
#include <fcntl.h>
#include <string>
#include <array>
#include <unordered_map>
#include "ctpl_stl.h"  // Include the provided thread pool header

#define PORT 8080
#define MAX_EVENTS 10
#define THREAD_COUNT 12

// Predefined credentials for authentication
std::unordered_map<std::string, std::string> credentials = {
    {"saxena", "am_i_dreaming"},
    {"user2", "password2"}
};

// Function to execute a shell command and return its output
std::string executeCommand(const std::string &cmd) {
    std::array<char, 128> buffer;
    std::string result;

    std::string full_command = cmd + " 2>&1";
    FILE* pipe = popen(full_command.c_str(), "r");
    if (!pipe) {
        return "Failed to execute command\n";
    }

    while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
        result += buffer.data();
    }

    pclose(pipe);
    return result;
}

// Set a socket to non-blocking mode
void setNonBlocking(int sock) {
    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);
}

bool authenticateClient(int clientSock) {
    char buffer[1024] = {0};

    // Read client response
    int bytesRead = read(clientSock, buffer, sizeof(buffer));
    if (bytesRead <= 0) {
        std::cerr << "Error reading credentials from client.\n";
        return false;
    }

    // Parse username and password
    std::string input(buffer, bytesRead);
    size_t spacePos = input.find(" ");  // Look for the space between username and password

    // If no space found, the format is invalid
    if (spacePos == std::string::npos) {
        const std::string error = "Invalid format. Use 'username password'.\n";
        send(clientSock, error.c_str(), error.size(), 0);
        return false;
    }

    // Extract username and password
    std::string username = input.substr(0, spacePos);  // Username is before space
    std::string password = input.substr(spacePos + 1);  // Password is after space

    // std::cout << "Extracted username: [" << username << "], password: [" << password << "]" << std::endl;
    // Validate credentials
    if (credentials.find(username) != credentials.end() && credentials[username] == password) {
        const std::string success = "AUTH_SUCCESS\n";
        send(clientSock, success.c_str(), success.size(), 0);
        return true;
    } else {
        const std::string failure = "AUTH_FAILED\n";
        send(clientSock, failure.c_str(), failure.size(), 0);
        return false;
    }
}


int main() {
    //create socket
    int serverSock = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSock < 0) {
        perror("Socket creation failed");
        return 1;
    }

    // Configure server address
    struct sockaddr_in serverAddr = {};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(PORT);
    serverAddr.sin_addr.s_addr = INADDR_ANY;

    // Bind socket
    if (bind(serverSock, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        perror("Bind failed");
        close(serverSock);
        return 1;
    }

    // Listen for connections
    if (listen(serverSock, SOMAXCONN) < 0) {
        perror("Listen failed");
        close(serverSock);
        return 1;
    }

    // std::cout<<"server is listening to port "<<PORT<<std::endl;
    setNonBlocking(serverSock);

    // Create epoll instance
    int epollFd = epoll_create1(0);
    if (epollFd < 0) {
        perror("Epoll creation failed");
        close(serverSock);
        return 1;
    }

    // Add server socket to epoll
    struct epoll_event event = {};
    event.events = EPOLLIN;
    event.data.fd = serverSock;
    epoll_ctl(epollFd, EPOLL_CTL_ADD, serverSock, &event);

    // Initialize the thread pool with THREAD_COUNT threads
    ctpl::thread_pool threadPool(THREAD_COUNT);
    std::cout << "Server is running on port " << PORT << std::endl;

    // Map to track authenticated clients
    std::unordered_map<int, bool> authenticatedClients;

    while (true) {
        struct epoll_event events[MAX_EVENTS];
        int eventCount = epoll_wait(epollFd, events, MAX_EVENTS, -1);
        if (eventCount < 0) {
            perror("Epoll wait failed");
            break;
        }

        for (int i = 0; i < eventCount; ++i) {
            if (events[i].data.fd == serverSock) {
                // Accept new connections
                int clientSock = accept(serverSock, nullptr, nullptr);
                if (clientSock < 0) {
                    perror("Accept failed");
                    continue;
                }
                setNonBlocking(clientSock);

                event.events = EPOLLIN | EPOLLET;
                event.data.fd = clientSock;
                epoll_ctl(epollFd, EPOLL_CTL_ADD, clientSock, &event);
                std::cout << "New client connected: " << clientSock << std::endl;

                authenticatedClients[clientSock] = false;

            } else {
                // Handle client input
                int clientSock = events[i].data.fd;

                threadPool.push([clientSock, &authenticatedClients](int threadId) {
                    if (!authenticatedClients[clientSock]) {
                        // Authenticate client
                        if (authenticateClient(clientSock)) {
                            authenticatedClients[clientSock] = true;
                            std::cout << "Client authenticated: " << clientSock << std::endl;
                        } else {
                            std::cout << "Authentication failed for client: " << clientSock << std::endl;
                            close(clientSock);
                        }
                        return;
                    }

                    // Handle commands for authenticated clients
                    char buffer[1024] = {0};
                    int bytesRead = read(clientSock, buffer, sizeof(buffer));
                    if (bytesRead <= 0) {
                        std::cout << "Client disconnected: " << clientSock << std::endl;
                        close(clientSock);
                        authenticatedClients.erase(clientSock);
                        return;
                    }

                    std::string command(buffer, bytesRead);
                    std::cout << "Thread " << threadId << " processing command: " << command << " from client: " << clientSock << std::endl;

                    // Execute the command
                    std::string output = executeCommand(command);

                    // Send response back to the client
                    send(clientSock, output.c_str(), output.size(), 0);
                });
            }
        }
    }

    close(serverSock);
    close(epollFd);
    return 0;
}
