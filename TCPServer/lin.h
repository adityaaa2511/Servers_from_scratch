#ifndef LINSERVER_H
#define LINSERVER_H

#include "./tcpserver.h"
#include <string>
#include <unordered_map>
#include "ctpl_stl.h"
#include <sys/epoll.h>

using namespace std;

struct ClientState{
    int client_socket;
    string recvBuffer;
    string sendBuffer;
    size_t bytesSent;
    bool waitingforSend;
    bool waitingforRecv;

    ClientState(): client_socket(-1),bytesSent(0),waitingforRecv(true),waitingforSend(false){}
    explicit ClientState(int csocket): client_socket(csocket),bytesSent(0),waitingforRecv(true),waitingforSend(false){}
};
class Linserver: public TCPServer{
public:
    Linserver();
    virtual ~Linserver();

    bool initialize(int port,const string &ip_address="0.0.0.0") override;
    void start() override;
    void handleClient(int client_socket);
    void setSocketNonBlocking(int sockedId);

private:
    int server_fd;
    int epoll_fd;
    unordered_map<int,ClientState> clientStates;
    thread epollThread;
    static ctpl::thread_pool threadPoll;
    static int determineThreadPoolSize();
    void handleSend(int client_socket);
    void handleRecv(int client_socket);
    void handleError(int client_socket);
    void epollLoop();
    void prepareAndSendResponse(int client_socket,const string &response);
    string getDateTimeHTMLResponse();
    bool isResquestComplete(int client_socket);

};
#endif