#include "../include/state.hpp"
#include "../include/http.hpp"


#include <iostream>
#include <string>
#include <sstream>
#include <thread>
#include <mutex>
#include <chrono> // for cleanup thread

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <random>
using namespace pb;

// Global mutex for match state
std::mutex g_mutex;

// Helpers
std::string generate_token_for_captain()
{
    static const char chars[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    thread_local std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<> dist(0, sizeof(chars) - 2);

    std::string s(24, '0');
    for (char &c : s)
    {
        c = chars[dist(rng)];
    }
    return s;
}

// Client handler
void handle_client(int client_fd)
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
        std::string resp = make_http_response("Bad Request\n", "text/plain", 400, "Bad Request");
        send(client_fd, resp.c_str(), resp.size(), 0);
        close(client_fd);
        return;
    }

    std::string resp;

    if (req.method == "GET" && req.path == "/match/create")
    {
        std::string teamA = get_query_param(req.query, "teamA");
        std::string teamB = get_query_param(req.query, "teamB");
        std::string series = get_query_param(req.query, "series");
        int slots = 0;      // default to BO1
        if(series == "bo1") {
            slots = 0;
        } else if(series == "bo3") {
            slots = 1;
        }
        else if (!series.empty())
        {
            resp = make_http_response("Unknown series type\n", "text/plain", 400, "Bad Request");
            send(client_fd, resp.c_str(), resp.size(), 0);
            close(client_fd);
            return;
        }

        std::lock_guard<std::mutex> lock(g_mutex);
        Match &m = create_match(teamA, teamB, slots);
        std::string body = "{\"matchId\":\"" + m.id + "\"}";
        resp = make_http_response(body, "application/json");
    }
    else if (req.method == "GET" && req.path == "/match/state")
    {
        std::string id = get_query_param(req.query, "id");
        std::lock_guard<std::mutex> lock(g_mutex);
        Match *m = get_match(id);
        if (!m)
        {
            resp = make_http_response("Match not found\n", "text/plain", 404, "Not Found");
        }
        else
        {
            std::string body = match_to_json(*m);
            resp = make_http_response(body, "application/json");
        }
    }
    else if (req.method == "GET" && req.path == "/match/action")
    {
        std::string id = get_query_param(req.query, "id");
        std::string teamStr = get_query_param(req.query, "team");
        std::string actStr = get_query_param(req.query, "action");
        std::string mapStr = get_query_param(req.query, "map");
        std::string token = get_query_param(req.query, "token");

        if (id.empty() || teamStr.empty() || actStr.empty() || mapStr.empty())
        {
            resp = make_http_response("Missing parameters\n", "text/plain", 400, "Bad Request");
        }
        else
        {
            int team = std::stoi(teamStr);
            int mapId = std::stoi(mapStr);

            ActionType at;
            if (actStr == "ban")
                at = ActionType::Ban;
            else if (actStr == "pick")
                at = ActionType::Pick;
            else
            {
                resp = make_http_response("Unknown action\n", "text/plain", 400, "Bad Request");
                send(client_fd, resp.c_str(), resp.size(), 0);
                close(client_fd);
                return;
            }

            std::lock_guard<std::mutex> lock(g_mutex);
            Match *m = get_match(id);
            if (!m)
            {
                resp = make_http_response("Match not found\n", "text/plain", 404, "Not Found");
            }
            else
            {
                if (team < 0 || team > 1)
                {
                    resp = make_http_response("Invalid team index\n", "text/plain", 400, "Bad Request");
                }
                else if (m->teamCaptainTokens[team].empty() || token.empty() || token != m->teamCaptainTokens[team])
                {
                    resp = make_http_response("Not authorized to be join this team.\n", "text/plain", 403, "Forbidden");
                }
                else
                {
                    bool ok = apply_action(*m, team, at, mapId);
                    if (!ok)
                    {
                        resp = make_http_response("Invalid action\n", "text/plain", 400, "Bad Request");
                    }
                    else
                    {
                        std::string body = match_to_json(*m);
                        resp = make_http_response(body, "application/json");
                    }
                }
            }
        }
    }
    else if (req.method == "GET" && req.path == "/match/join")
    {
        std::string id = get_query_param(req.query, "id");
        std::string teamStr = get_query_param(req.query, "team"); // "0", "1", or "spectator"
        std::string token = get_query_param(req.query, "token");  // optional existing token


        std::lock_guard<std::mutex> lock(g_mutex);
        Match *m = get_match(id);
        if (!m)
        {
            resp = make_http_response("Match not found\n", "text/plain", 404, "Not Found");
        }
        else
        {
            std::string role = "spectator";
            int teamIndex = -1;
            std::string outToken;


            if (teamStr == "spectator")
            {
                // spectator: always allowed, no token needed
                role = "spectator";
            }
            else
            {
                // try to parse team index 0 or 1
                try
                {
                    teamIndex = std::stoi(teamStr);
                }
                catch (...)
                {
                    teamIndex = -1;
                }


                if (teamIndex < 0 || teamIndex > 1)
                {
                    resp = make_http_response("Invalid team index\n", "text/plain", 400, "Bad Request");
                    send(client_fd, resp.c_str(), resp.size(), 0);
                    close(client_fd);
                    return;
                }


                std::string &currentToken = m->teamCaptainTokens[teamIndex];
                if (currentToken.empty())
                {
                    // no captain yet: claim captain for this team
                    currentToken = generate_token_for_captain();
                    outToken = currentToken;
                    role = "captain";
                }
                else
                {
                    // captain already exists
                    if (!token.empty() && token == currentToken)
                    {
                        // same client rejoining
                        outToken = currentToken;
                        role = "captain";
                    }
                    else
                    {
                        // someone else trying to join as captain -> spectator
                        role = "spectator";
                    }
                }
            }


            std::ostringstream body;
            body << "{";
            body << "\"matchId\":\"" << m->id << "\",";
            body << "\"role\":\"" << role << "\"";
            if (teamIndex >= 0)
            {
                body << ",\"team\":" << teamIndex;
            }
            if (!outToken.empty())
            {
                body << ",\"token\":\"" << outToken << "\"";
            }
            body << "}";


            resp = make_http_response(body.str(), "application/json");
        }
    }


    else
    {
        resp = make_http_response("Valorant BO3 map veto server\n", "text/plain");
    }

    send(client_fd, resp.c_str(), resp.size(), 0);
    close(client_fd);
}

int main()
{
    init_state();

    // cleanup thread
    std::thread([]()
                {
        using namespace std::chrono_literals;
        while (true) {
            std::this_thread::sleep_for(10min);
            std::lock_guard<std::mutex> lock(g_mutex);
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

    std::cout << "Listening on http://localhost:" << port << "\n";

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

        std::thread(handle_client, client_fd).detach();
    }

    close(server_fd);
    return 0;
}
