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
    std::vector<int> targets;
    {
        std::lock_guard<std::mutex> lock(ctx.wsClientsMutex);
        targets.reserve(8);
        for (const auto &c : ctx.wsClients) {
            if (c.matchId == m.id) targets.push_back(c.fd);
        }
    }

    for (int fd : targets) {
        send_ws_text(fd, payload);
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
        if (ctx.wsClients.capacity() == 0) ctx.wsClients.reserve(64);
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
        for (size_t i = 0; i < v.size(); ++i) {
            if (v[i].fd == client_fd) {
                v[i] = v.back();
                v.pop_back();
                break;
            }
        }
    }

    close(client_fd);
}