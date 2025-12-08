#include "../include/match.hpp"
#include "../include/state.hpp"
#include "../include/websockets.hpp"

using namespace pb;

MatchContext& get_match_context() {
    static MatchContext ctx;
    return ctx;
}

void broadcast_match_update(const pb::Match& m) {
    std::string payload = pb::match_to_json(m);

    auto& ctx = get_match_context();
    std::lock_guard<std::mutex> lock(ctx.wsClientsMutex);
    for (auto it = ctx.wsClients.begin(); it != ctx.wsClients.end(); ++it) {
        if (it->matchId == m.id) {
            send_ws_text(it->fd, payload);
        }
    }
}

void handle_websocket_client(int client_fd) {
    std::string msg;
    if (!recv_ws_frame(client_fd, msg)) {
        close(client_fd);
        return;
    }

    std::string matchId = msg;
    if (matchId.empty()) {
        close(client_fd);
        return;
    }

    auto& ctx = get_match_context();
    {
        std::lock_guard<std::mutex> lock(ctx.wsClientsMutex);
        ctx.wsClients.push_back(WsClient{client_fd, matchId});
    }

    {
        std::lock_guard<std::mutex> lock(ctx.matchMutex);
        pb::Match* m = pb::get_match(matchId);
        if (m) {
            send_ws_text(client_fd, pb::match_to_json(*m));
        }
    }

    while (true) {
        std::string payload;
        if (!recv_ws_frame(client_fd, payload)) {
            break;
        }
    }

    {
        std::lock_guard<std::mutex> lock(ctx.wsClientsMutex);
        auto& v = ctx.wsClients;
        v.erase(std::remove_if(v.begin(), v.end(),
                               [client_fd](const WsClient& c) {
                                   return c.fd == client_fd;
                               }),
                v.end());
    }

    close(client_fd);
}