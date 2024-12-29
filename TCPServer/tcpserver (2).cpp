#include "./tcpserver.h"


#ifdef PLATFORM_LINUX
#include "./lin.h"
#endif

// Factory function that returns the correct implementation based on the OS
TCPServer *createServer()
{
#ifdef PLATFORM_LINUX
    return new LinServer();
#endif
}