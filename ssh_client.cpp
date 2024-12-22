#include <iostream>
#include <string>
#include <unistd.h>
#include <cstring>
#include <sys/socket.h>
#include <arpa/inet.h>

#define SERVER_IP "127.0.0.1"  // Server address
#define SERVER_PORT 8080       // Server port

int main() {
    // Create socket
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("Socket creation failed");
        return 1;
    }

    // Set up server address
    struct sockaddr_in serverAddr = {};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(SERVER_PORT);
    serverAddr.sin_addr.s_addr = inet_addr(SERVER_IP);

    // Connect to the server
    if (connect(sock, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        perror("Connection failed");
        close(sock);
        return 1;
    }

    std::cout<<"Connected to SSH server"<<std::endl;
    // Authentication
    std::string username, password;
    std::cout << "Username:";

    // Read the full input (username and password)
    std::getline(std::cin, username);  // Read username
    std::cout << "Password: ";
    std::getline(std::cin, password);  // Read password

    // Combine the username and password into a single string with a space
    std::string credentials = username + " " + password;

    // Send credentials to the server
    send(sock, credentials.c_str(), credentials.size(), 0);

    // Buffer to read the response from the server
    char buffer[1024] = {0};
    int bytesRead = read(sock, buffer, sizeof(buffer));
    if (bytesRead <= 0) {
        std::cerr << "Error reading response from server.\n";
        close(sock);
        return 1;
    }

    // Convert buffer to string to easily check the response
    std::string serverResponse(buffer, bytesRead);

    // Check the response
    std::cout<<serverResponse<<std::endl;
    if (serverResponse == "AUTH_SUCCESS\n") {
        std::cout << "Authentication successful!\n";

        // Start the command execution loop
        while (true) {
            std::cout << "Enter command (type 'exit' to quit): ";
            std::string command;
            std::getline(std::cin, command);

            if (command == "exit") {
                break;  // Exit the loop
            }

            // Send command to the server
            send(sock, command.c_str(), command.size(), 0);

            // Receive and display the output from the server
            bytesRead = read(sock, buffer, sizeof(buffer));
            if (bytesRead <= 0) {
                std::cerr << "Error reading command output from server.\n";
                break;
            }

            std::cout << "Command output:\n" << std::string(buffer, bytesRead) << std::endl;
        }
    } else {
        std::cout << "Authentication failed.\n";
    }

    close(sock);
    return 0;
}
