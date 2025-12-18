#include "../include/state.hpp"
#include "../include/http.hpp"
#include "../include/match.hpp"
#include "../include/http_router.hpp"
#include "../include/websockets.hpp"
#include "../include/match.hpp"
#include <openssl/sha.h>
#include <openssl/evp.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>

#include <iostream>
#include <string>
#include <sstream>
#include <thread>
#include <mutex>
#include <chrono> 

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <random>
#include <algorithm>
using namespace pb;

int main()
{
    init_state();

    // cleanup thread
    std::thread([]()
                {
        using namespace std::chrono_literals;
        while (true) {
            std::this_thread::sleep_for(10min);
            auto& ctx = get_match_context();
            std::lock_guard<std::mutex> lock(ctx.matchMutex);
            pb::prune_old_matches(std::chrono::hours(1));
        } })
        .detach();

    int port = 8080;
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (server_fd < 0)
    {
        perror("socket");
        return 1;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(server_fd, (sockaddr *)&addr, sizeof(addr)) < 0)
    {
        perror("bind");
        close(server_fd);
        return 1;
    }

    if (listen(server_fd, SOMAXCONN) < 0)
    {
        perror("listen");
        close(server_fd);
        return 1;
    }

    std::cout << "Listening on " << port << "\n";

    while (true)
    {
        sockaddr_in clientAddr{};
        socklen_t clientLen = sizeof(clientAddr);
        int client_fd = accept(server_fd, (sockaddr *)&clientAddr, &clientLen);
        if (client_fd < 0)
        {
            perror("accept");
            continue;
        }

        std::thread(handle_client_connection, client_fd).detach();
    }

    close(server_fd);
    return 0;
}
