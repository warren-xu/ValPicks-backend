#include <sstream>
#include "../include/match_http.hpp"
#include "../include/match.hpp"
#include "../include/state.hpp"
#include "../include/http.hpp"

using namespace pb;

// Helpers
std::string generate_captain_id()
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

std::string handle_match_http(const HttpRequest &req)
{
    auto &ctx = get_match_context();

    if (req.method == "GET" && req.path == "/match/create")
    {
        std::string teamA = get_query_param(req.query, "teamA");
        std::string teamB = get_query_param(req.query, "teamB");
        std::string series = get_query_param(req.query, "series");

        std::lock_guard<std::mutex> lock(ctx.matchMutex);
        Match &m = create_match(teamA, teamB, series);
        std::string body = "{\"matchId\":\"" + m.id + "\"}";
        return make_http_response(body, "application/json");
    }
    else if (req.method == "GET" && req.path == "/match/state")
    {
        std::string id = get_query_param(req.query, "id");
        std::lock_guard<std::mutex> lock(ctx.matchMutex);
        Match *m = get_match(id);
        if (!m)
        {
            return make_http_response("Match not found\n", "text/plain", 404, "Not Found");
        }
        else
        {
            std::string body = match_to_json(*m);
            return make_http_response(body, "application/json");
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
            return make_http_response("Missing parameters\n", "text/plain", 400, "Bad Request");
        }

        int team = std::stoi(teamStr);
        int mapId = std::stoi(mapStr);

        ActionType at;
        if (actStr == "ban")
            at = ActionType::Ban;
        else if (actStr == "pick")
            at = ActionType::Pick;
        else
            return make_http_response("Unknown action\n", "text/plain", 400, "Bad Request");

        auto &ctx = get_match_context();
        std::lock_guard<std::mutex> lock(ctx.matchMutex);
        Match *m = get_match(id);
        if (!m)
        {
            return make_http_response("Match not found\n", "text/plain", 404, "Not Found");
        }

        if (team < 0 || team > 1)
        {
            return make_http_response("Invalid team index\n", "text/plain", 400, "Bad Request");
        }
        else if (m->teamCaptainTokens[team].empty() || token.empty() || token != m->teamCaptainTokens[team])
        {
            return make_http_response("Not authorized to be join this team.\n", "text/plain", 403, "Forbidden");
        }

        bool ok = apply_action(*m, team, at, mapId);
        if (!ok)
        {
            return make_http_response("Invalid action\n", "text/plain", 400, "Bad Request");
        }

        broadcast_match_update(*m);
        std::string body = match_to_json(*m);
        return make_http_response(body, "application/json");
    }

    else if (req.method == "GET" && req.path == "/match/join")
    {
        std::string id = get_query_param(req.query, "id");
        std::string teamStr = get_query_param(req.query, "team");
        std::string token = get_query_param(req.query, "token");  // optional existing token

        std::lock_guard<std::mutex> lock(ctx.matchMutex);
        Match *m = get_match(id);
        if (!m)
        {
            return make_http_response("Match not found\n", "text/plain", 404, "Not Found");
        }
        else
        {
            std::string role = "spectator";
            int teamIndex = -1;
            std::string outToken;

            if (teamStr == "spectator")
            {
                // no token needed for spectator
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
                    return make_http_response("Invalid team index\n", "text/plain", 400, "Bad Request");
                }

                std::string &currentToken = m->teamCaptainTokens[teamIndex];
                if (currentToken.empty())
                {
                    // claim captain for team if no captain was claimed yet
                    currentToken = generate_captain_id();
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
                        // redirect to spectator
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

            return make_http_response(body.str(), "application/json");
        }
    }

    else
    {
        return make_http_response("Valorant BO3 map veto server\n", "text/plain");
    }
}