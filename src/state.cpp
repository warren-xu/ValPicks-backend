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
        static const char chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
        std::uniform_int_distribution<> dist(0, sizeof(chars) - 2); // -2 to exclude null terminator
        std::string s(6, '0');
        for (char &c : s)
        {
            c = chars[dist(rng())];
        }
        return s;
    }

    static std::vector<Map> get_default_maps()
    {
        std::vector<std::string> names = {
            "Abyss", "Ascent", "Bind", "Haven", "Icebox", "Lotus", "Split"};

        std::vector<std::string> previewUrls = {
            "public/videos/abyss.mp4",
            "public/videos/ascent.mp4",
            "public/videos/bind.mp4",
            "public/videos/havenb.mp4",
            "public/videos/icebox.mp4",
            "public/videos/lotus.mp4",
            "public/videos/split.mp4"};
        std::vector<std::string> mapImgUrls = {
            "public/mapimgs/abyss.webp",
            "public/mapimgs/ascent.webp",
            "public/mapimgs/bind.webp",
            "public/mapimgs/haven.webp",
            "public/mapimgs/icebox.webp",
            "public/mapimgs/lotus.webp",
            "public/mapimgs/split.webp"};
        std::vector<Map> result;
        int id = 1;
        for (uint64_t i = 0; i < names.size(); ++i)
        {
            result.push_back(Map{id++, names[i], previewUrls[i], mapImgUrls[i]});
        }
        return result;
    }

    static std::vector<Step> bo1_steps()
    {
        return {
            {ActionType::Ban, TEAM_A},  // Team A Bans
            {ActionType::Ban, TEAM_B},  // Team B Bans
            {ActionType::Ban, TEAM_A},  // Team A Bans
            {ActionType::Ban, TEAM_A},  // Team B Bans
            {ActionType::Pick, TEAM_B}, // Team A Picks the Map
            {ActionType::Side, TEAM_A}, // Team B Picks the Side
        };
    }

    static std::vector<Step> bo3_steps() // Bo3 system for valorant
    {
        return {
            {ActionType::Ban, TEAM_A},
            {ActionType::Ban, TEAM_B},

            {ActionType::Pick, TEAM_A}, // Team A Picks Map 1
            {ActionType::Side, TEAM_B}, // Team B Picks Side for Map 1

            {ActionType::Pick, TEAM_B}, // Team B Picks Map 2
            {ActionType::Side, TEAM_A}, // Team A Picks Side for Map 2

            {ActionType::Ban, TEAM_A},
            {ActionType::Ban, TEAM_B},

            {ActionType::Side, TEAM_A}, // Team A Picks Side for Decider map
        };
    }

    static bool is_map_available(const Match &m, int mapId)
    {
        for (const auto &team : m.teams)
        {
            for (int b : team.bannedMapIds)
            {
                if (b == mapId)
                    return false;
            }

            for (int p : team.pickedMapIds)
            {
                if (p == mapId)
                    return false;
            }
        }

        if (m.deciderMapId == mapId)
            return false;
        return true;
    }

    void init_state()
    {
        g_matches.clear();
    }

    Match &create_match(const std::string &teamAName, const std::string &teamBName, std::string series)
    {
        Match m;
        m.id = generate_match_id();
        m.phase = Phase::BanPhase;
        m.currentTurnTeam = TEAM_A;
        m.currentStepIndex = 0; // steps are zero-indexed
        m.availableMaps = get_default_maps();
        m.steps = bo1_steps();
        m.deciderMapId = 0;
        m.deciderSide = -1;
        m.deciderSidePickerTeam = -1;
        m.seriesType = series;
        m.lastUpdated = std::chrono::steady_clock::now();
        m.teamCaptainTokens[TEAM_A].clear();
        m.teamCaptainTokens[TEAM_B].clear();

        if (series == "bo3")
        {
            m.steps = bo3_steps();
        }

        m.stepMapIds.assign(m.steps.size(), pb::UNASSIGNED_MAP_ID);
        m.stepSideVals.assign(m.steps.size(), -1);

        m.teams[TEAM_A].name = teamAName;
        m.teams[TEAM_B].name = teamBName;

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
            return false;

        const Step &currentStep = m.steps[m.currentStepIndex];
        if (currentStep.teamIndex != teamIndex || currentStep.action != action)
            return false;

        const auto stepIdx = m.currentStepIndex;

        if (action == ActionType::Ban)
        {
            if (!is_map_available(m, mapId)) return false;
            m.teams[teamIndex].bannedMapIds.push_back(mapId);
            m.stepMapIds[stepIdx] = mapId;
        }
        else if (action == ActionType::Pick)
        {
            if (!is_map_available(m, mapId)) return false;
            m.teams[teamIndex].pickedMapIds.push_back(mapId);
            m.currentSideMapId = mapId;
            m.stepMapIds[stepIdx] = mapId;
        }
        else if (action == ActionType::Side)
        {
            const int side = mapId; // mapId arg is treated as side (0 or 1) here
            // If this is the decider map (last step), record it specifically
            if (m.seriesType == "bo1") {
                 m.deciderSide = side;
                 m.deciderSidePickerTeam = teamIndex;
                 m.deciderMapId = m.currentSideMapId; 
            }
            else if (m.currentSideMapId == m.deciderMapId && m.deciderMapId != 0) {
                 m.deciderSide = side;
                 m.deciderSidePickerTeam = teamIndex;
            }

            // store side choice for that map
            if (m.currentSideMapId != pb::UNASSIGNED_MAP_ID)
            {
                m.mapSides[m.currentSideMapId] = side;
                m.stepMapIds[stepIdx] = m.currentSideMapId;
                m.stepSideVals[stepIdx] = side;
            }
        }

        // Advance to next step
        m.currentStepIndex++;

        // check completion
        if (m.currentStepIndex >= m.steps.size())
        {
            m.phase = Phase::Completed;
            
            // Logic for bo1: each side bans 2, team a picks the map
            if (m.seriesType == "bo1" && m.deciderMapId == 0) {
                 if (!m.teams[TEAM_B].pickedMapIds.empty()) {
                    m.deciderMapId = m.teams[TEAM_B].pickedMapIds[0];
                }
            }
        }
        else
        {
            const auto &nextStep = m.steps[m.currentStepIndex];
            
            m.currentTurnTeam = nextStep.teamIndex;

            // calculate decider map if we are at the last step of bo3
            if (m.seriesType == "bo3" && m.currentStepIndex == m.steps.size() - 1)
            {
                for (const auto &map : m.availableMaps)
                {
                    if (is_map_available(m, map.id))
                    {
                        m.deciderMapId = map.id;
                        m.currentSideMapId = map.id;
                        m.stepMapIds[m.currentStepIndex] = map.id;
                        break;
                    }
                }
            }
            if (nextStep.action == ActionType::Pick)
                m.phase = Phase::PickPhase;
            else if (nextStep.action == ActionType::Side)
                m.phase = Phase::SidePhase;
            else
                m.phase = Phase::BanPhase;
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
        std::string seriesType = m.seriesType;
        oss << "\"seriesType\":\"" << seriesType << "\",";
        oss << "\"captainTaken\":["
            << (m.teamCaptainTokens[0].empty() ? false : true) << ","
            << (m.teamCaptainTokens[1].empty() ? false : true) << "],";
        oss << "\"deciderSide\":" << m.deciderSide << ",";
        oss << "\"deciderSidePickerTeam\":" << m.deciderSidePickerTeam << ",";
        oss << "\"teams\":[";
        for (int i = 0; i < 2; ++i)
        {
            const Team &team = m.teams[i];
            oss << "{";
            oss << "\"name\":\"" << team.name << "\",";

            oss << "\"bannedMapIds\":[";
            for (size_t j = 0; j < team.bannedMapIds.size(); ++j)
            {
                oss << team.bannedMapIds[j];
                if (j + 1 < team.bannedMapIds.size())
                    oss << ",";
            }
            oss << "],";

            oss << "\"pickedMapIds\":[";
            for (size_t j = 0; j < team.pickedMapIds.size(); ++j)
            {
                oss << team.pickedMapIds[j];
                if (j + 1 < team.pickedMapIds.size())
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
            oss << "\"name\":\"" << map.name << "\",";
            oss << "\"previewUrl\":\"" << map.previewUrl << "\",";
            oss << "\"mapImgUrl\":\"" << map.mapImgUrl << "\"";
            oss << "}";
            if (i + 1 < m.availableMaps.size())
                oss << ",";
        }

        oss << "],";
        oss << "\"stepMapIds\":[";
        for (size_t i = 0; i < m.stepMapIds.size(); ++i)
        {
            oss << m.stepMapIds[i];
            if (i + 1 < m.stepMapIds.size())
                oss << ",";
        }
        oss << "],";

        oss << "\"stepSideVals\":[";
        for (size_t i = 0; i < m.stepSideVals.size(); ++i)
        {
            oss << m.stepSideVals[i];
            if (i + 1 < m.stepSideVals.size())
                oss << ",";
        }
        oss << "],";
        oss << "\"steps\":[";
        for (size_t i = 0; i < m.steps.size(); ++i)
        {
            oss << "{";
            oss << "\"action\":" << static_cast<int>(m.steps[i].action) << ",";
            oss << "\"teamIndex\":" << m.steps[i].teamIndex;
            oss << "}";
            if (i + 1 < m.steps.size())
                oss << ",";
        }
        oss << "]";
        oss << "}";
        return oss.str();
    }
    std::string match_to_light_json(const Match &m)
{
    std::ostringstream oss;
    oss << "{";
    oss << "\"id\":\"" << m.id << "\",";
    oss << "\"phase\":" << static_cast<int>(m.phase) << ",";
    oss << "\"currentTurnTeam\":" << m.currentTurnTeam << ",";
    oss << "\"currentStepIndex\":" << m.currentStepIndex << ",";
    oss << "\"deciderMapId\":" << m.deciderMapId << ",";
    
    oss << "\"deciderSide\":" << m.deciderSide << ",";
    oss << "\"deciderSidePickerTeam\":" << m.deciderSidePickerTeam << ",";

    oss << "\"captainTaken\":["
        << (m.teamCaptainTokens[0].empty() ? false : true) << ","
        << (m.teamCaptainTokens[1].empty() ? false : true) << "],";

    oss << "\"teams\":[";
    for (int i = 0; i < 2; ++i)
    {
        const Team &team = m.teams[i];
        oss << "{";

        oss << "\"bannedMapIds\":[";
        for (size_t j = 0; j < team.bannedMapIds.size(); ++j)
        {
            oss << team.bannedMapIds[j];
            if (j + 1 < team.bannedMapIds.size()) oss << ",";
        }
        oss << "],";

        oss << "\"pickedMapIds\":[";
        for (size_t j = 0; j < team.pickedMapIds.size(); ++j)
        {
            oss << team.pickedMapIds[j];
            if (j + 1 < team.pickedMapIds.size()) oss << ",";
        }
        oss << "]";
        oss << "}";
        if (i + 1 < 2) oss << ",";
    }
    oss << "],";

    oss << "\"stepMapIds\":[";
    for (size_t i = 0; i < m.stepMapIds.size(); ++i)
    {
        oss << m.stepMapIds[i];
        if (i + 1 < m.stepMapIds.size()) oss << ",";
    }
    oss << "],";

    oss << "\"stepSideVals\":[";
    for (size_t i = 0; i < m.stepSideVals.size(); ++i)
    {
        oss << m.stepSideVals[i];
        if (i + 1 < m.stepSideVals.size()) oss << ",";
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