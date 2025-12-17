#include "http_router.hpp"
#include "../include/http.hpp"
#include "../include/websockets.hpp"
#include "../include/match.hpp"
#include "../include/match_http.hpp"

#include <unistd.h>
#include <string>
#include <sstream>

void handle_client_connection(int client_fd)
{
    char buffer[4096];
    int bytes = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
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
