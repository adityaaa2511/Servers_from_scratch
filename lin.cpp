#include "./lin.h"
#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netinet/tcp.h>
#include <cstring>
#include <thread>
#include <mutex>
#include <fcntl.h>
#include <ctime>
#include <cstring>
#include <sstream>
#include <errno.h> // For errno

using namespace std;
// Buffer size for receiving messages
#define BUFFER_SIZE 1024

#define MAX_EVENTS 10

mutex coutmutex; //Mmutex to sync the output on console. 

using namespace std;

Linserver::Linserver(): server_fd(-1),epoll_fd(-1)
LinServer::~LinServer()
{
    if (server_fd != -1)
    {
        close(server_fd);
    }
    if (epollThread.joinable())
    {
        epollThread.join();
    }
}

// Initialize the static thread pool using the hardware concurrency
ctpl::thread_pool LinServer::threadPool(LinServer::determineThreadPoolSize());

int LinServer::determineThreadPoolSize()
{
    int concurrency = thread::hardware_concurrency();
    return (concurrency > 0) ? concurrency : 4; // Default to 4 if hardware concurrency is unavailable
}

bool LinServer::setSocketNonBlocking(int socketId)
{
    int flags = fcntl(socketId, F_GETFL, 0);
    if (flags == -1)
    {
        cout << "Error getting socket flags!" << endl;
        return false;
    }

    flags |= O_NONBLOCK;
    if (fcntl(socketId, F_SETFL, flags) == -1)
    {
        cout<< "Error setting socket to non-blocking!" <<endl;
        return false;
    }

    cout << "Socket set to non-blocking mode." << endl;
    return true;
}

void LinServer::handleClient(int client_socket)
{
    char buffer[BUFFER_SIZE] = {0};

    // Continue to receive data until the client closes the connection
    while (true)
    {
        // Clear the buffer before receiving new data
        memset(buffer, 0, sizeof(buffer));

        // Receive data from the client
        int valread = read(client_socket, buffer, BUFFER_SIZE);

        // If the read is less than or equal to 0, the client has closed the connection
        if (valread <= 0)
        {
            lock_guard<mutex> lock(coutMutex);
            cout << "Client disconnected or error occurred. Closing connection." << endl;
            break; // Exit the loop to close the connection
        }

        // Log the received data
        {
            lock_guard<mutex> lock(coutMutex);
            cout << "Received: " << buffer << endl;
        }

        // Prepare the HTTP response
        string http_response =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/plain\r\n"
            "Content-Length: " +
            to_string(valread) + "\r\n"
                                      "\r\n" +
            string(buffer, valread); // Echo the received data

        // Send the HTTP response back to the client
        send(client_socket, http_response.c_str(), http_response.length(), 0);

        {
            lock_guard<mutex> lock(coutMutex);
            cout << "Echo response sent!" << endl;
        }
    }

    // Close the connection
    close(client_socket);
    {
        lock_guard<mutex> lock(coutMutex);
        cout << "Client connection closed." << endl;
    }
}

bool LinServer::initialize(int port, const string &ip_address)
{
    cout << "initialising the server..." << endl;
    struct sockaddr_in address;
    int opt = 1;


    // Create socket file descriptor
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0)
    {
        cout << "Socket creation failed!" << endl;
        return false;
    }

    // Set socket options to reuse address and port
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt)))
    {
        cout << "setsockopt failed!" << endl;
        return false;
    }

    epoll_fd = epoll_create1(0);
    if (epoll_fd == -1)
    {
        cout << "Epoll creation failed!" << endl;
        return false;
    }

    // Set up the server address structure
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY; // all available ip addresses
    address.sin_port = htons(port);       // Set the port number

    // Bind the socket to the IP address and port
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0)
    {
        cout << "Bind failed!" << endl;
        return false;
    }

    // Start listening for connections
    if (listen(server_fd, SOMAXCONN) < 0)
    {
        cout << "Listen failed!" << endl;
        return false;
    }

    cout << "Server initialized on " << ip_address << ":" << port << endl;
    return true;
}

void LinServer::start()
{
    epollThread = thread(&LinServer::epollLoop, this);
    struct sockaddr_in client_address;
    socklen_t addrlen = sizeof(client_address);
    char buffer[BUFFER_SIZE] = {0};
    int client_socket = -1;

    cout << "Waiting for connections..." << endl;

    while (true)
    { // Infinite loop to accept connections continuously
        // Accept an incoming connection
        if ((client_socket = accept(server_fd, (struct sockaddr *)&client_address, &addrlen)) < 0)
        {
            cout << "Accept failed! Error: " << strerror(errno) << endl;
            continue; // Continue to the next iteration to accept new connections
        }
        //cout << "Connection accepted!" << endl;


        // Set client socket to non-blocking
        setSocketNonBlocking(client_socket);
        // Add the new client socket to the epoll set for monitoring
        struct epoll_event ev;
        ev.events = EPOLLIN | EPOLLOUT | EPOLLERR | EPOLLET;
        ev.data.fd = client_socket;
        epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_socket, &ev);
        //cout << "New client connected and added to epoll." << endl;

        //also initialise the client socket state here so that we can store this socket to use later
        clientStates[client_socket] = ClientState(client_socket); // Initialize client state

    }
}

void LinServer::epollLoop()
{
    struct epoll_event events[MAX_EVENTS];

    while (true)
    {
        int event_count = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        for (int i = 0; i < event_count; i++)
        {
            int fd = events[i].data.fd;

            if (events[i].events & EPOLLIN)
            {
                // Push to thread pool for receiving data
                threadPool.push([this, fd](int thread_id)
                                { this->handleRecv(fd); });
            }
            else if (events[i].events & EPOLLOUT)
            {
                // Push to thread pool for sending data
                threadPool.push([this, fd](int thread_id)
                                { this->handleSend(fd); });
            }
            else if (events[i].events & EPOLLERR)
            {
                // Handle errors
                threadPool.push([this, fd](int thread_id)
                                { this->handleError(fd); });
            }
        }
    }
}

// Function to get the current date and time in HTML format
string LinServer::getDateTimeHTMLResponse()
{
    // The HTML content includes a script to display the time based on the client's timezone
    string htmlResponse =
        "<html><head><title>Echo Server</title></head>"
        "<body><h1>Echo Server</h1>"
        "<p>Below is the current time in your timezone:</p>"
        "<p style='color:red;' id='localTime'></p>"
        "<script>"
        "function getLocalTime() {"
        "  const now = new Date();"
        "  const options = { weekday: 'long', year: 'numeric', month: 'long', day: 'numeric', hour: 'numeric', minute: 'numeric', second: 'numeric', timeZoneName: 'short' };"
        "  const localTime = now.toLocaleString(undefined, options);"
        "  document.getElementById('localTime').innerText = localTime;"
        "}"
        "window.onload = getLocalTime;"
        "</script>"
        "</body></html>";

    string httpResponse =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html\r\n"
        "Content-Length: " +
        to_string(htmlResponse.size()) + "\r\n"
                                              "\r\n" + htmlResponse;

    return httpResponse;
}

void LinServer::handleRecv(int client_socket)
{
    // Ensure client state is initialized the first time
    if (clientStates.find(client_socket) == clientStates.end())
    {
        clientStates[client_socket] = ClientState(client_socket); // Initialize client state
    }

    auto &state = clientStates[client_socket]; // Access the client's state
    char buffer[BUFFER_SIZE];
    int bytesRead = recv(client_socket, buffer, BUFFER_SIZE, 0); 

    if (bytesRead > 0)
    {
        // cout << "Received data from client: " << string(buffer, bytesRead) << endl;

        state.recvBuffer.append(buffer, bytesRead);

        // Check if the request is complete (e.g., HTTP would check for \r\n\r\n or content length)
        if (isRequestComplete(state.client_socket))
        {
            string response=getDateTimeHTMLResponse();
            prepareAndSendResponse(client_socket,response);
        }
        else{
            state.waitingforRecv=true;
        }
    }
    else if (bytesRead == 0)
    {
        cout << "Client disconnected." << endl;
        close(client_socket);
        clientStates.erase(client_socket); // Clean up state
    }
    else
    {
        if (errno != EWOULDBLOCK && errno != EAGAIN)
        {
            cout << "Error in recv: " << strerror(errno) << endl;
            handleError(client_socket);
        }
        else
        {
            // more data to recv
            return;
        }
    }
}

void LinServer::handleSend(int client_socket)
{
    auto &state = clientStates[client_socket]; // access client state

    if (state.bytesSent < state.sendBuffer.size())
    {
        // Calculate how much more data needs to be sent
        int bytesToSend = state.sendBuffer.size() - state.bytesSent;
        int bytesSent = send(client_socket, state.sendBuffer.c_str() + state.bytesSent, bytesToSend, 0); 

        if (bytesSent > 0)
        {
            state.bytesSent += bytesSent; // Update the number of bytes sent so far
            if (state.bytesSent == state.sendBuffer.size())
            {
                // If all data has been sent, reset the state
                cout << "All data sent to client." << endl;
                state.bytesSent=0;
                state.sendBuffer.clear();
                state.waitingforSend=false;
                state.waitingforRecv=true;
            }
        }
        else
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                // wait for the next opportunity to send
                return;
            }
            else
            {
                // An error occurred, handle it
                cout << "Error in send." << endl;
                handleError(client_socket);
            }
        }
    }
}

void LinServer::handleError(int client_socket)
{
    cout << "Socket error, closing connection." << endl;
    close(client_socket);
}

// Check if the request has been fully received
bool LinServer::isRequestComplete(int client_socket)
{
    clientStates[client_socket].recvBuffer.find("/r/n/r/n")!=string::npos;
}

void Linserver::prepareAndSendResponse(int client_socket,const string &response){
    auto &clientState=clientStates[client_socket];
    clientState.client_socket=client_socket;
    clientState.sendBuffer=response;
    clientState.bytesSent=0;

    handleSend(client_socket);
}
