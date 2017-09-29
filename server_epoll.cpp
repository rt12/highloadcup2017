#include "server_epoll.h"
#include <fcntl.h>
#include <unistd.h>

#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <unistd.h>
#include <netinet/tcp.h>
#include <sched.h>

#include <array>
#include <functional>
#include <list>
#include <thread>

#include <boost/pool/object_pool.hpp>

#include "connection.h"

int make_socket_non_blocking (int sfd) 
{
    int flags, s;

    flags = fcntl(sfd, F_GETFL, 0);
    if (flags == -1)
    {
        perror("fcntl");
        return -1;
    }

    flags |= O_NONBLOCK;
    s = fcntl(sfd, F_SETFL, flags);
    if (s == -1)
    {
        perror("fcntl");
        return -1;
    }

    return 0;
}

int create_and_bind(short port) 
{
    struct sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    serv_addr.sin_addr.s_addr = INADDR_ANY;

    int sfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sfd == -1)
        return sfd;

    int enable = 1;
    setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int));
    setsockopt(sfd, SOL_SOCKET, SO_REUSEPORT, &enable, sizeof(int));

    setsockopt(sfd, IPPROTO_TCP, TCP_NODELAY, &enable, sizeof(int));
    setsockopt(sfd, IPPROTO_TCP, TCP_QUICKACK, &enable, sizeof(int));

    int retval = bind(sfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr));
    if (retval != 0) {
        close(sfd);
        fprintf(stderr, "Could not bind\n");
        return -1;
    }

    return sfd;
}

pid_t gettid( void )
{
    return syscall(SYS_gettid);
}

int set_thread_affinity(int cpu)
{
    cpu_set_t set;

    CPU_ZERO(&set);
    CPU_SET(cpu, &set);
    if (sched_setaffinity( gettid(), sizeof( cpu_set_t ), &set )) {
        perror( "sched_setaffinity" );
    }
}

struct EpollHandler 
{
    int fd;
    
    EpollHandler(int openfd = -1) : fd(openfd) {}

    virtual ~EpollHandler()
    {
        close();
    }

    virtual void close()
    {
        if (fd != -1) {
            ::close(fd);
            fd = -1;
        }
    }

    virtual void onIdle(int efd)
    {
    }

    virtual void handleEvent(int efd, uint32_t events)
    {
        // fprintf(stderr, "fd: %08d, events: %08x\n", fd, events);

        if (events&(EPOLLHUP|EPOLLERR)) {
            close();
        }
    }
};

struct EpollConnection: public EpollHandler, public ConnectionBase
{
    int efd;
    std::array<char, 8192> readBuf;
    bool addedToEpoll;
    std::array<iovec, 100> writeBufs;
    size_t writeIndex;
    size_t writeIoCount;
    bool watching = false;

    std::function<void()> onClose;

    EpollConnection(int fd, int epollfd, Handler& handler) : 
        EpollHandler(fd), 
        ConnectionBase(handler),
        efd(epollfd),
        addedToEpoll(false),
        writeIndex(0),
        writeIoCount(0)
    {
        int i = 1;
        // don't let inactive sockets die
        setsockopt(fd, SOL_SOCKET,SO_KEEPALIVE, &i, sizeof(i));
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &i, sizeof(i));
    }

    ~EpollConnection()
    {
    }

    void add(uint32_t events = EPOLLIN)
    {
        epoll_event ev;
        ev.events = events | EPOLLET | EPOLLONESHOT;
        ev.data.ptr = this;
        int action = watching ? EPOLL_CTL_MOD : EPOLL_CTL_ADD;

        if (epoll_ctl(efd, action, fd, &ev) < 0) {
            fprintf(stderr, "epoll_ctl error: fd=%d\n", fd);
            return;
        }

        watching = true;
    }

    virtual void startRead() override
    {
        ssize_t rc = read(fd, readBuf.data(), readBuf.size());

        // fprintf(stderr, "read: rc=%zd\n", rc);

        if (rc == -1) {
            if (errno == EAGAIN) {
                add();
                // fprintf(stderr, "No data on start...\n");
            } else {
                if (errno != 104)
                   fprintf(stderr, "read error: %d\n", errno);
                close();
            }

            return;
        }

        if (rc == 0) {
            close();
            return;
        }

        uv_buf_t buf{readBuf.data(), readBuf.size()};
        onRead(rc, &buf);

        // fprintf(stderr, "read %zd bytes, sending reply\n", rc);
        // write(reply, sizeof(reply));
    }
    
    void startWrite()
    {
        if (writeIoCount == 0) {
            return;
        }

        ssize_t rc = ::writev(fd, &writeBufs[writeIndex], writeIoCount);

        //fprintf(stderr, "Write index=%d, ioc=%d, rc=%d\n", writeIndex, writeIoCount, rc);

        if (rc == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                fprintf(stderr, "Write will block\n");
                add(EPOLLOUT | EPOLLIN);
            } else {
                fprintf(stderr, "write error: %d\n", errno);
                close();
            }
            return;
        }

        // update buffer pointers
        
        while (rc > 0) {
            auto bufp = (uv_buf_t*)(&writeBufs[writeIndex]);

            if (bufp->len > rc) {
                bufp->len -= rc;
                bufp->base += rc;
                break;
            }
            
            rc -= bufp->len;
            ++writeIndex;
            --writeIoCount;
        }

        if (writeIoCount == 0) {
            writeIndex = 0;
            // fprintf(stderr, "Write completed, %zd bytes sent\n", rc);
            onWriteComplete();
        } else {
            // partial write
            add(EPOLLOUT | EPOLLIN);
        }
    }


    virtual void close() override
    {
        // fprintf(stderr, "close(%d)\n", fd);
        // fprintf(stderr, "Write index=%d, ioc=%d\n", writeIndex, writeIoCount);
        if (fd != -1) {
            EpollHandler::close();
            if (onClose)
                onClose();
        }
    }

    virtual void handleEvent(int efd, uint32_t events) override
    {  
        if (events&(EPOLLHUP|EPOLLERR) && !(events&EPOLLIN)) {
            fprintf(stderr, "[%d] EPOLLHUP\n", fd);
            close();
            return;
        }

        if (events & EPOLLOUT) {
            startWrite();
        }

        if (events & EPOLLIN) {
            startRead();
        }

    }

    void addWriteBuf(const void* data, size_t size)
    {
        if (size == 0)
            return;

        size_t idx = writeIndex + writeIoCount;
        assert(idx < writeBufs.size());
        writeBufs[idx].iov_base = (void*)data;
        writeBufs[idx].iov_len = size;
        ++writeIoCount;
    }
    
    void write(const void* data, size_t size)
    {
        addWriteBuf(data, size);
        startWrite();
    }
    
    virtual void writeResponse(int result) override
    {
        // fprintf(stderr, "writeResponse: %d\n", result);
        d_response.code = static_cast<HttpStatus>(result);
        formatHeaders(d_response);

        addWriteBuf(d_headersRef.data(), d_headersRef.size());
        addWriteBuf(d_response.data(), d_response.size());

        startWrite();
    }
};

struct EpollListener: public EpollHandler
{
    boost::object_pool<EpollConnection> d_pool;
    Handler& d_handler;
    uint64_t d_count = 0;
    bool watching = false;
    bool edgeTriggered = true;

    EpollListener(int fd, Handler& handler) 
        : EpollHandler(fd), d_handler(handler) {}

    int add(int efd)
    {
        int r = 0;
        epoll_event ev;
        ev.events = EPOLLIN | EPOLLONESHOT;
        if (edgeTriggered)
            ev.events |= EPOLLET;
        ev.data.ptr = this;
        auto action = watching ? EPOLL_CTL_MOD : EPOLL_CTL_ADD;

        if ((r = epoll_ctl(efd, action, fd, &ev)) < 0) {
            fprintf(stderr, "epoll_ctl error: fd=%d, e=%d\n", fd, errno);
        }

        watching = true;
        return r;
    }

    int startListen(int efd)
    {
        int r = ::listen(fd, 2048);
        if (r < 0) {
            fprintf(stderr, "listen() failed: %d\n", errno);
            return r;
        }

        return add(efd);
    }

    virtual void onIdle(int efd)
    {
        // fprintf(stderr, "Listener [%d] served %lu connections\n", fd, d_count);
    }

    virtual void handleEvent(int efd, uint32_t events)
    {
        struct sockaddr in_addr;
        socklen_t in_len = sizeof(in_addr);

        for(;;) {
            int infd = accept4(fd, &in_addr, &in_len, SOCK_NONBLOCK);
            if (infd < 0) {
                if (errno != EAGAIN && errno != EWOULDBLOCK) {
                    fprintf(stderr, "accept4 failed: %d\n", errno);
                }

                break;
            }

            auto conn = d_pool.construct(infd, efd, std::ref(d_handler));

            conn->onClose = [this, conn]() {
                d_pool.destroy(conn);
                // fprintf(stderr, "Closing connection (total: %zu)\n", connections.size());
            };

            conn->startRead();
            ++d_count;
        }

        add(efd);
    }
};

ServerEpoll::ServerEpoll(short port, Handler& handler)
    : d_handler(handler), d_port(port)
{
}

ServerEpoll::~ServerEpoll()
{
}

void ServerEpoll::run(int threadsCount)
{
    int listenSock = create_and_bind(d_port);
    make_socket_non_blocking(listenSock);
    fprintf(stdout, "Listen socket: %d\n", listenSock);

    int efd = epoll_create1(0);
    EpollListener listener(listenSock, d_handler);
    listener.startListen(efd);
            
    std::vector<std::thread> threads;

    for (int i = 0; i < threadsCount; ++i) {
        threads.emplace_back([this, i, &listener, efd]() {
               // set_thread_affinity(i); 
               dispatch(efd, listener); });
    } 

    for (auto& t: threads)
        t.join();
}

void ServerEpoll::dispatch(int efd, EpollListener& l)
{
    const size_t MAXEVENTS = 1;
    epoll_event events[MAXEVENTS];

    while(true) {
        int n = epoll_wait(efd, events, MAXEVENTS, 1000);
        if (n == -1 && errno != EINTR) {
            fprintf(stderr, "epoll_wait error: %d\n", errno);
            return;
        }

        if (n == 0) {
            l.onIdle(efd);
        }

        for (int i = 0; i < n; ++i) {
           const auto& ev = events[i];
           auto ef = ev.events;
           if (ev.data.ptr) {
               static_cast<EpollHandler*>(ev.data.ptr)->handleEvent(efd, ef);
           }
        }
    }
}

