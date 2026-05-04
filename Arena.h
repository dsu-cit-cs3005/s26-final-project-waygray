#pragma once

#include <chrono>
#include <map>
#include <optional>
#include <random>
#include <string>
#include <vector>

#include "RadarObj.h"
#include "RobotBase.h"

struct ArenaConfig {
    int height = 20;
    int width = 20;
    int max_rounds = 1000;
    double sleep_interval_seconds = 0.0;
    bool game_state_live = false;
    int flamethrowers = 5;
    int pits = 5;
    int mounds = 5;
};

class Arena {
public:
    explicit Arena(const ArenaConfig& config);
    ~Arena();

    bool initialize();
    void run();

private:
    struct RobotEntry {
        RobotBase* robot = nullptr;
        void* handle = nullptr;
        std::string source_file;
        char symbol = '?';
        int row = 0;
        int col = 0;
        bool alive = true;
        int grenade_shots_fired = 0;
    };

    struct Cell {
        bool mound = false;
        bool pit = false;
        bool flame = false;
    };

    ArenaConfig m_config;
    std::mt19937 m_rng;
    int m_round = 1;

    std::vector<std::vector<Cell>> m_cells;
    std::vector<RobotEntry> m_robots;

    bool m_initialized = false;

    bool parse_robot_files(std::vector<std::string>& robot_sources) const;
    bool load_robots();
    bool compile_and_load_robot(const std::string& source_file, char symbol);

    void place_obstacles();
    void place_robots();

    void print_round_banner() const;
    void print_arena() const;

    int count_alive() const;
    std::optional<std::size_t> winner_index() const;

    std::vector<RadarObj> scan_radar_for_robot(const RobotEntry& entry, int direction) const;
    std::vector<std::pair<int, int>> directional_band_cells(int row, int col, int direction, int max_depth = -1) const;

    bool take_robot_turn(RobotEntry& entry);
    bool handle_shot(RobotEntry& shooter, int shot_row, int shot_col);
    void apply_weapon_damage_to_cells(RobotEntry& shooter, const std::vector<std::pair<int, int>>& impacted_cells);
    void apply_damage(RobotEntry& attacker, RobotEntry& target, int min_damage, int max_damage, const std::string& cause);

    bool handle_move(RobotEntry& entry, int move_direction, int move_distance);

    bool in_bounds(int row, int col) const;
    bool cell_has_live_robot(int row, int col) const;
    bool cell_has_dead_robot(int row, int col) const;
    bool cell_has_any_robot(int row, int col) const;
    RobotEntry* robot_at(int row, int col);
    const RobotEntry* robot_at(int row, int col) const;

    char board_cell_symbol(int row, int col) const;

    int random_int(int low, int high);

    void cleanup();

    static std::string trim(const std::string& text);
    static std::string normalize_key(const std::string& key);
};