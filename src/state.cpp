#include "../include/state.hpp"

#include <random>
#include <sstream>
#include <algorithm>

namespace pb
{
    // Global Storage
    static std::unordered_map<std::string, Match> g_matches;
    static std::mt19937 &rng()
    {
        static std::mt19937 gen(std::random_device{}());
        return gen;
    }

    // Helpers
    std::string generate_match_id()
    {
        static const char chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
        std::uniform_int_distribution<> dist(0, sizeof(chars) - 2); // -2 to exclude null terminator
        std::string s(8, '0');
        for (char &c : s)
        {
            c = chars[dist(rng())];
        }
        return s;
    }

    static std::vector<Map> get_default_maps()
    {
        std::vector<std::string> names = {
            "Abyss", "Bind", "Corrode", "Haven", "Pearl", "Split", "Sunset"};

        std::vector<Map> result;
        int id = 1;
        for (auto &n : names)
        {
            result.push_back(Map{id++, n});
        }
        return result;
    }

    static std::vector<Step> bo1_steps()
{       // 7-map pool BO1 system
    return {
        {ActionType::Ban, 0},
        {ActionType::Ban, 1},
        {ActionType::Ban, 0},
        {ActionType::Ban, 1},
        {ActionType::Ban, 0},
        {ActionType::Ban, 1},
    };
}

    static std::vector<Step> bo3_steps()        // Bo3 system for valorant
    {
        return {
            {ActionType::Ban, TEAM_A},
            {ActionType::Ban, TEAM_B},
            {ActionType::Pick, TEAM_A},
            {ActionType::Pick, TEAM_B},
            {ActionType::Ban, TEAM_A},
            {ActionType::Ban, TEAM_B},
        };
    }

    static bool is_map_available(const Match &m, int mapId)
    {
        for (const auto &team : m.teams)
        {
            for (int b : team.bannedMapIds)
            {
                if (b == mapId)
                {
                    return false;
                }
            }

            // already picked?
            for (const auto &slot : team.slots)
                if (slot.mapId == mapId)
                    return false;
        }
        return true;
    }

    void init_state()
    {
        g_matches.clear();
    }

    Match &create_match(const std::string &teamAName, const std::string &teamBName, int slotsPerTeam)
    {
        Match m;
        m.id = generate_match_id();
        m.phase = Phase::BanPhase;
        m.currentTurnTeam = TEAM_A;
        m.currentStepIndex = 0;     // steps are zero-indexed
        m.availableMaps = get_default_maps();
        m.steps = bo1_steps();
        m.deciderMapId = 0;
        m.lastUpdated = std::chrono::steady_clock::now();
        m.teamCaptainTokens[TEAM_A].clear();
        m.teamCaptainTokens[TEAM_B].clear();
        m.slotsPerTeam = slotsPerTeam;

        if(slotsPerTeam == 1) {
            m.steps = bo3_steps();
        }

        m.teams[TEAM_A].name = teamAName;
        m.teams[TEAM_B].name = teamBName;

        for (int i = 0; i < slotsPerTeam; ++i)
        {
            m.teams[TEAM_A].slots.push_back(TeamSlot{});
            m.teams[TEAM_B].slots.push_back(TeamSlot{});
        }

        g_matches[m.id] = m;
        return g_matches[m.id];
    }

    Match *get_match(const std::string &matchId)
    {
        auto it = g_matches.find(matchId);
        if (it != g_matches.end())
        {
            return &it->second;
        }
        return nullptr;
    }

    bool apply_action(Match &m, int teamIndex, ActionType action, int mapId)
    {
        if (m.phase == Phase::Completed || m.currentStepIndex >= m.steps.size())
        {
            return false; // Match already completed
        }

        const Step &currentStep = m.steps[m.currentStepIndex];
        if (currentStep.teamIndex != teamIndex || currentStep.action != action)
        {
            return false; // Not this team's turn or wrong action
        }

        if (!is_map_available(m, mapId))
        {
            return false; // Map already banned or picked
        }

        if (action == ActionType::Ban)
        {
            m.teams[teamIndex].bannedMapIds.push_back(mapId);
        }
        else if (action == ActionType::Pick)
        {
            // Assign map to the first available slot
            for (auto &slot : m.teams[teamIndex].slots)
            {
                if (slot.mapId == UNASSIGNED_MAP_ID) // 0 means unassigned
                {
                    slot.mapId = mapId;
                    break;
                }
            }
        }

        // Advance to next step
        m.currentStepIndex++;
        if (m.currentStepIndex >= m.steps.size())
        {
            m.phase = Phase::Completed;

            int remainingMapId = -1;
            for (const auto &map : m.availableMaps)
            {
                if (is_map_available(m, map.id))
                {
                    remainingMapId = map.id;
                    break;
                }
            }
            m.deciderMapId = remainingMapId;
        }
        else
        {
            m.currentTurnTeam = m.steps[m.currentStepIndex].teamIndex;
            if (m.steps[m.currentStepIndex].action == ActionType::Pick)
            {
                m.phase = Phase::PickPhase;
            }
            else
            {
                m.phase = Phase::BanPhase;
            }
        }

        m.lastUpdated = std::chrono::steady_clock::now();

        return true;
    }

    std::string match_to_json(const Match &m)
    {
        std::ostringstream oss;
        oss << "{";
        oss << "\"id\":\"" << m.id << "\",";
        oss << "\"phase\":" << static_cast<int>(m.phase) << ",";
        oss << "\"currentTurnTeam\":" << m.currentTurnTeam << ",";
        oss << "\"currentStepIndex\":" << m.currentStepIndex << ",";
        oss << "\"deciderMapId\":" << m.deciderMapId << ",";
        std::string seriesType = (m.slotsPerTeam == 0) ? "bo1" : "bo3";
        oss << "\"seriesType\":\"" << seriesType << "\",";

        oss << "\"teams\":[";
        for (int i = 0; i < 2; ++i)
        {
            const Team &team = m.teams[i];
            oss << "{";
            oss << "\"name\":\"" << team.name << "\",";
            oss << "\"slots\":[";
            for (size_t j = 0; j < team.slots.size(); ++j)
            {
                const TeamSlot &slot = team.slots[j];
                oss << "{";
                oss << "\"playerName\":\"" << slot.playerName << "\",";
                oss << "\"mapId\":" << slot.mapId;
                oss << "}";
                if (j + 1 < team.slots.size())
                    oss << ",";
            }
            oss << "],";

            oss << "\"bannedMapIds\":[";
            for (size_t j = 0; j < team.bannedMapIds.size(); ++j)
            {
                oss << team.bannedMapIds[j];
                if (j + 1 < team.bannedMapIds.size())
                    oss << ",";
            }
            oss << "]";

            oss << "}";
            if (i + 1 < 2)
                oss << ",";
        }
        oss << "],";

        oss << "\"availableMaps\":[";
        for (size_t i = 0; i < m.availableMaps.size(); ++i)
        {
            const Map &map = m.availableMaps[i];
            oss << "{";
            oss << "\"id\":" << map.id << ",";
            oss << "\"name\":\"" << map.name << "\"";
            oss << "}";
            if (i + 1 < m.availableMaps.size())
                oss << ",";
        }
        oss << "]";

        oss << "}";
        return oss.str();
    }

    void prune_old_matches(std::chrono::seconds maxAge)
    {
        auto now = std::chrono::steady_clock::now();

        for (auto it = g_matches.begin(); it != g_matches.end();)
        {
            auto age = now - it->second.lastUpdated;
            if (age > maxAge)
            {
                it = g_matches.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

}