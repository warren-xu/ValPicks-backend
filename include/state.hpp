#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <iostream>
#include <chrono>


namespace pb {
    const int TEAM_A = 0;
    const int TEAM_B = 1;
    const int UNASSIGNED_MAP_ID = 0;
    enum class Phase {
        BanPhase,
        PickPhase,
        Completed
    };

    enum class ActionType {
        Ban,
        Pick
    };

    struct Map {
        int id;
        std::string name;
        
    };

    struct TeamSlot {
        std::string playerName;
        int mapId;
    };

    struct Team {
        std::string name;
        std::vector<TeamSlot> slots;
        std::vector<int> bannedMapIds;
    };

    struct Step {
        ActionType action;
        int teamIndex;
    };

    struct Match {
        std::string id;

        Phase phase;
        int currentTurnTeam;                // Index of the team whose turn it is 
        std::size_t currentStepIndex;       // Index of the current step in the pick/ban sequence
        std::chrono::steady_clock::time_point lastUpdated;
        int slotsPerTeam;

        Team teams[2];
        std::vector<Map> availableMaps;
        std::vector<Step> steps;
        std::string teamCaptainTokens[2];

        int deciderMapId = 0;
    };
    void init_state();
    Match& create_match(const std::string& teamAName, const std::string& teamBName, int slotsPerTeam);
    Match* get_match(const std::string& matchId);

    bool apply_action(Match& m, int teamIndex, ActionType action, int mapId);
    std::string match_to_json(const Match& m);
    std::string generate_match_id();
    void prune_old_matches(std::chrono::seconds maxAge);
};

