#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <map>
#include <iostream>
#include <chrono>

namespace pb
{
    const int TEAM_A = 0;
    const int TEAM_B = 1;
    const int UNASSIGNED_MAP_ID = 0;
    enum class Phase
    {
        BanPhase = 0,
        PickPhase = 1,
        SidePhase = 2,
        Completed = 3
    };

    enum class ActionType
    {
        Ban,
        Pick,
        Side
    };

    struct Map
    {
        int id;
        std::string name;
        std::string previewUrl;
        std::string mapImgUrl;
    };

    struct TeamSlot
    {
        std::string playerName;
        int mapId;
    };

    struct Team
    {
        std::string name;
        std::vector<int> bannedMapIds;
        std::vector<int> pickedMapIds;
    };

    struct Step
    {
        ActionType action;
        int teamIndex;
    };

    struct Match
    {
        std::string id;

        Phase phase;
        int currentTurnTeam;          // Index of the team whose turn it is
        std::size_t currentStepIndex; // Index of the current step in the pick/ban sequence
        std::chrono::steady_clock::time_point lastUpdated;
        std::string seriesType;

        Team teams[2];
        std::vector<Map> availableMaps;
        std::vector<Step> steps;
        std::string teamCaptainTokens[2];
        int deciderSide = -1;
        int deciderSidePickerTeam = -1;

        int deciderMapId = 0;

        std::map<int, int> mapSides;   // Key: MapID, Value: 0 (Attack) or 1 (Defend)
        int currentSideMapId = 0;      // The ID of the map we are currently picking a side for
        std::vector<int> stepMapIds;   // map chosen for each step index (0 if not set)
        std::vector<int> stepSideVals; // side for Side steps: -1 unset, 0 atk, 1 def
    };
    void init_state();
    Match &create_match(const std::string &teamAName, const std::string &teamBName, std::string series);
    Match *get_match(const std::string &matchId);

    bool apply_action(Match &m, int teamIndex, ActionType action, int mapId);
    std::string match_to_json(const Match &m);
    std::string match_to_light_json(const Match &m);
    std::string generate_match_id();
    void prune_old_matches(std::chrono::seconds maxAge);
};
