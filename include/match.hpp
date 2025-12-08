#pragma once
#include <mutex>
#include <vector>
#include <string>
#include <random>
#include <algorithm>
#include <unistd.h>
#include "../include/state.hpp"

struct WsClient {
    int fd;
    std::string matchId;
};

struct MatchContext {
    std::mutex matchMutex;
    std::mutex wsClientsMutex;
    std::vector<WsClient> wsClients;
};


MatchContext& get_match_context();
void handle_websocket_client(int client_fd);
void broadcast_match_update(const pb::Match& m);