#include "database.h"
#include "loader.h"
#include "handler.h"
#include "connection.h"
#include "server_epoll.h"

#include <thread>
#include <fstream>

#define DEFAULT_PORT 80
#define DEFAULT_BACKLOG 1000

int main(int argc, char **argv)
{
    short port = DEFAULT_PORT;
    int threadsCount = 8;

    if (argc < 2)
        return 1;

    if (argc > 2)
        port = atoi(argv[2]);

    if (argc > 3)
        threadsCount = atoi(argv[3]);
    
    std::ifstream ofs(std::string(argv[1]) + "/options.txt");
    uint32_t now;
    uint32_t isfull = 0;

    ofs >> now >> isfull;

    Database db;
    db.reserve(isfull != 0);

    Handler handler(db);
    Loader loader(db);
    loader.loadDirectory(argv[1]);

    std::cout << "Using port " << port << std::endl;
    std::cout << "Threads: " << threadsCount << std::endl;
    std::cout << "Timestamp: " << now << std::endl;
    
    db.setNow(now);

    ServerEpoll server(port, handler);
    server.run(threadsCount);

    return 0;
}

