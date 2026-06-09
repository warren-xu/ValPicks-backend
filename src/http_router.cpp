#include "http_router.hpp"
#include "../include/http.hpp"
#include "../include/websockets.hpp"
#include "../include/match.hpp"
#include "../include/match_http.hpp"

#include <unistd.h>
#include <errno.h>
#include <poll.h>
#include <cstring>
#include <string>
#include <sstream>

static ssize_t recv_http_request(int fd, char *buffer, size_t capacity)
{
    size_t total = 0;
    while (total + 1 < capacity)
    {
        ssize_t n = recv(fd, buffer + total, capacity - 1 - total, 0);
        if (n > 0)
        {
            total += static_cast<size_t>(n);
            buffer[total] = '\0';
            if (strstr(buffer, "\r\n\r\n") != nullptr)
                return static_cast<ssize_t>(total);
            continue;
        }
        if (n == 0)
            return 0;
        if (errno == EINTR)
            continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK)
        {
            struct pollfd pfd{};
            pfd.fd = fd;
            pfd.events = POLLIN;
            if (poll(&pfd, 1, -1) <= 0)
                return -1;
            continue;
        }
        return -1;
    }
    return static_cast<ssize_t>(total);
}

void handle_client_connection(int client_fd)
{
    char buffer[4096];
    ssize_t bytes = recv_http_request(client_fd, buffer, sizeof(buffer));
    if (bytes <= 0)
    {
        close(client_fd);
        return;
    }
    buffer[bytes] = '\0';
    std::string raw(buffer);

    HttpRequest req;
    if (!parse_http_request(raw, req))
    {
        std::string resp = make_http_response(
            "Bad Request\n", "text/plain", 400, "Bad Request");
        send(client_fd, resp.c_str(), resp.size(), 0);
        close(client_fd);
        return;
    }

    if (req.method == "OPTIONS")
    {
        // return empty 204 with CORS headers
        std::string resp = make_http_response("", "text/plain", 204, "No Content");
        send(client_fd, resp.c_str(), resp.size(), 0);
        close(client_fd);
        return;
    }
    // WebSocket upgrade
    if (req.method == "GET" && req.path == "/ws")
    {
        std::string secKey;
        if (!is_websocket_upgrade(raw, secKey))
        {
            std::string resp = make_http_response(
                "Bad WS upgrade\n", "text/plain", 400, "Bad Request");
            send(client_fd, resp.c_str(), resp.size(), 0);
            close(client_fd);
            return;
        }

        std::string acceptKey = compute_websocket_accept(secKey);

        std::ostringstream hs;
        hs << "HTTP/1.1 101 Switching Protocols\r\n"
           << "Upgrade: websocket\r\n"
           << "Connection: Upgrade\r\n"
           << "Sec-WebSocket-Accept: " << acceptKey << "\r\n"
           << "\r\n";
        std::string handshake = hs.str();
        send(client_fd, handshake.c_str(), handshake.size(), 0);

        handle_websocket_client(client_fd);
        return;
    }

    // normal HTTP
    std::string resp = handle_match_http(req);
    send(client_fd, resp.c_str(), resp.size(), 0);
    close(client_fd);
}
