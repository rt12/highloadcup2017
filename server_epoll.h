#include <sys/epoll.h>
#include <vector>

class Handler;
class EpollListener;

class ServerEpoll
{
    Handler& d_handler;
    short d_port;
    void dispatch(int efd, EpollListener& l);

public:

    ServerEpoll(short port, Handler& handler);
    ~ServerEpoll();

    void run(int threadsCount);
};


