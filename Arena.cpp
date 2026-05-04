#include "Arena.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <set>
#include <sstream>
#include <thread>

#include <dlfcn.h>

namespace {
constexpr int kRadarMinDirection = 0;
constexpr int kRadarMaxDirection = 8;
constexpr int kMoveMinDirection = 0;
constexpr int kMoveMaxDirection = 8;
constexpr std::size_t kMaxSummaryChars = 50;

bool has_robot_filename_shape(const std::string& filename) {
    return filename.size() >= 11 && filename.rfind("Robot_", 0) == 0 &&
           filename.size() > 4 && filename.substr(filename.size() - 4) == ".cpp";
}

std::string shell_quote(const std::string& input) {
    std::string out;
    out.reserve(input.size() + 2);
    out.push_back('\'');
    for (const char ch : input) {
        if (ch == '\'') {
            out += "'\\''";
        } else {
            out.push_back(ch);
        }
    }
    out.push_back('\'');
    return out;
}

std::pair<int, int> normalize_direction_delta(int from_row, int from_col, int to_row, int to_col) {
    const int dr = to_row - from_row;
    const int dc = to_col - from_col;
    const int ndr = (dr > 0) ? 1 : (dr < 0 ? -1 : 0);
    const int ndc = (dc > 0) ? 1 : (dc < 0 ? -1 : 0);
    return {ndr, ndc};
}

std::vector<std::pair<int, int>> ray_to_edge_dda(
    int from_row,
    int from_col,
    int target_row,
    int target_col,
    int row_max,
    int col_max)
{
    std::vector<std::pair<int, int>> cells;

    const int dr = target_row - from_row;
    const int dc = target_col - from_col;
    if (dr == 0 && dc == 0) {
        return cells;
    }

    const int major = std::max(std::abs(dr), std::abs(dc));
    const double row_inc = static_cast<double>(dr) / static_cast<double>(major);
    const double col_inc = static_cast<double>(dc) / static_cast<double>(major);

    int step = 1;
    while (true) {
        const int row = static_cast<int>(std::lround(from_row + row_inc * step));
        const int col = static_cast<int>(std::lround(from_col + col_inc * step));
        if (row < 0 || row >= row_max || col < 0 || col >= col_max) {
            break;
        }

        if (cells.empty() || cells.back().first != row || cells.back().second != col) {
            cells.push_back({row, col});
        }

        ++step;
    }

    return cells;
}

int armor_adjusted_damage(int raw_damage, int armor) {
    const double reduction = std::clamp(armor * 0.1, 0.0, 0.9);
    const double adjusted = raw_damage * (1.0 - reduction);
    return std::max(0, static_cast<int>(std::lround(adjusted)));
}
} // namespace

Arena::Arena(const ArenaConfig& config)
    : m_config(config),
      m_rng(static_cast<std::mt19937::result_type>(std::chrono::steady_clock::now().time_since_epoch().count())),
      m_cells(static_cast<std::size_t>(config.height), std::vector<Cell>(static_cast<std::size_t>(config.width))) {}

Arena::~Arena() {
    cleanup();
}

bool Arena::initialize() {
    if (m_initialized) {
        return true;
    }

    if (m_config.height <= 0 || m_config.width <= 0) {
        std::cerr << "Arena dimensions must be positive.\n";
        return false;
    }

    place_obstacles();

    if (!load_robots()) {
        cleanup();
        return false;
    }

    if (m_robots.empty()) {
        std::cerr << "No robots were loaded. Add Robot_*.cpp under robots/ or project root.\n";
        cleanup();
        return false;
    }

    place_robots();
    m_initialized = true;
    return true;
}

void Arena::run() {
    if (!m_initialized && !initialize()) {
        return;
    }

    while (m_round <= m_config.max_rounds) {
        for (auto& robot : m_robots) {
            print_round_banner();
            print_arena();

            const auto winner = winner_index();
            if (winner.has_value()) {
                std::cout << "Winner: " << m_robots[*winner].robot->m_name
                          << " (" << m_robots[*winner].symbol << ")\n";
                return;
            }

            if (!robot.alive) {
                std::cout << m_round << ": " << robot.robot->m_name << " is out.\n";
                ++m_round;
                continue;
            }

            take_robot_turn(robot);

            if (m_config.game_state_live && m_config.sleep_interval_seconds > 0.0) {
                std::this_thread::sleep_for(std::chrono::duration<double>(m_config.sleep_interval_seconds));
            }

            ++m_round;
            if (m_round > m_config.max_rounds) {
                break;
            }
        }
    }

    std::cout << "Reached max rounds without a single winner.\n";
    int best_health = -1;
    std::string best_name = "none";
    for (const auto& robot : m_robots) {
        if (robot.robot->get_health() > best_health) {
            best_health = robot.robot->get_health();
            best_name = robot.robot->m_name;
        }
    }
    std::cout << "Highest remaining health: " << best_name << " (" << best_health << ")\n";
}

bool Arena::parse_robot_files(std::vector<std::string>& robot_sources) const {
    robot_sources.clear();

    const std::filesystem::path robots_dir("robots");
    if (std::filesystem::exists(robots_dir) && std::filesystem::is_directory(robots_dir)) {
        for (const auto& entry : std::filesystem::directory_iterator(robots_dir)) {
            if (!entry.is_regular_file()) {
                continue;
            }
            const std::string filename = entry.path().filename().string();
            if (has_robot_filename_shape(filename)) {
                robot_sources.push_back(entry.path().string());
            }
        }
    }

    // Fallback for starter layout where robot files are in project root.
    if (robot_sources.empty()) {
        for (const auto& entry : std::filesystem::directory_iterator(".")) {
            if (!entry.is_regular_file()) {
                continue;
            }
            const std::string filename = entry.path().filename().string();
            if (has_robot_filename_shape(filename)) {
                robot_sources.push_back(entry.path().string());
            }
        }
    }

    std::sort(robot_sources.begin(), robot_sources.end());
    return !robot_sources.empty();
}

bool Arena::compile_and_load_robot(const std::string& source_file, char symbol) {
    using RobotSummaryFn = const char* (*)();

    const std::filesystem::path source_path(source_file);
    const std::string stem = source_path.stem().string();
    const std::string shared_lib = "lib" + stem + ".so";

    std::ostringstream compile_cmd;
    compile_cmd << "g++ -shared -fPIC -o " << shell_quote(shared_lib)
                << " " << shell_quote(source_file)
                << " RobotBase.o -I. -std=c++20";

    std::cout << "Compiling " << source_file << "...\n";
    if (std::system(compile_cmd.str().c_str()) != 0) {
        std::cerr << "Failed to compile " << source_file << "\n";
        return false;
    }

    void* handle = dlopen(shared_lib.c_str(), RTLD_LAZY);
    if (!handle) {
        std::cerr << "Failed to load " << shared_lib << ": " << dlerror() << "\n";
        return false;
    }

    RobotFactory create_robot = reinterpret_cast<RobotFactory>(dlsym(handle, "create_robot"));
    if (!create_robot) {
        std::cerr << "Missing create_robot in " << shared_lib << "\n";
        dlclose(handle);
        return false;
    }

    RobotSummaryFn summary_fn = reinterpret_cast<RobotSummaryFn>(dlsym(handle, "robot_summary"));
    if (!summary_fn) {
        std::cerr << "Missing robot_summary in " << shared_lib << "\n";
        dlclose(handle);
        return false;
    }

    const char* summary = summary_fn();
    if (!summary) {
        std::cerr << "robot_summary returned null in " << shared_lib << "\n";
        dlclose(handle);
        return false;
    }

    const std::size_t summary_len = std::strlen(summary);
    if (summary_len == 0 || summary_len > kMaxSummaryChars) {
        std::cerr << "robot_summary must be 1-" << kMaxSummaryChars
                  << " characters in " << shared_lib << "\n";
        dlclose(handle);
        return false;
    }

    RobotBase* robot = create_robot();
    if (!robot) {
        std::cerr << "create_robot failed in " << shared_lib << "\n";
        dlclose(handle);
        return false;
    }

    robot->set_boundaries(m_config.height, m_config.width);

    RobotEntry entry;
    entry.robot = robot;
    entry.handle = handle;
    entry.source_file = source_file;
    entry.symbol = symbol;
    entry.alive = true;

    if (robot->m_name.empty() || robot->m_name == "Blank_Robot") {
        robot->m_name = stem;
    }
    robot->m_character = symbol;

    std::cout << "Loaded " << robot->m_name << " as " << symbol << ": " << summary << "\n";
    m_robots.push_back(entry);
    return true;
}

bool Arena::load_robots() {
    std::vector<std::string> robot_sources;
    if (!parse_robot_files(robot_sources)) {
        return false;
    }

    const std::string symbol_pool = "@#$%&!*+=?^~ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";

    for (std::size_t i = 0; i < robot_sources.size(); ++i) {
        const char symbol = symbol_pool[i % symbol_pool.size()];
        if (!compile_and_load_robot(robot_sources[i], symbol)) {
            std::cerr << "Skipping robot source: " << robot_sources[i] << "\n";
        }
    }

    return !m_robots.empty();
}

void Arena::place_obstacles() {
    auto place_random = [&](int count, auto setter) {
        int placed = 0;
        int guard = 0;
        const int max_guard = m_config.height * m_config.width * 10;
        while (placed < count && guard < max_guard) {
            ++guard;
            const int row = random_int(0, m_config.height - 1);
            const int col = random_int(0, m_config.width - 1);
            Cell& cell = m_cells[static_cast<std::size_t>(row)][static_cast<std::size_t>(col)];
            if (cell.mound || cell.pit || cell.flame) {
                continue;
            }
            setter(cell);
            ++placed;
        }
    };

    place_random(m_config.mounds, [](Cell& c) { c.mound = true; });
    place_random(m_config.pits, [](Cell& c) { c.pit = true; });
    place_random(m_config.flamethrowers, [](Cell& c) { c.flame = true; });
}

void Arena::place_robots() {
    std::set<std::pair<int, int>> used;

    for (auto& entry : m_robots) {
        int guard = 0;
        const int max_guard = m_config.height * m_config.width * 10;
        while (guard < max_guard) {
            ++guard;
            const int row = random_int(0, m_config.height - 1);
            const int col = random_int(0, m_config.width - 1);
            const Cell& cell = m_cells[static_cast<std::size_t>(row)][static_cast<std::size_t>(col)];
            if (cell.mound || cell.pit || cell.flame) {
                continue;
            }
            if (used.count({row, col}) != 0) {
                continue;
            }
            used.insert({row, col});
            entry.row = row;
            entry.col = col;
            entry.robot->move_to(row, col);
            break;
        }
    }
}

void Arena::print_round_banner() const {
    std::cout << "\n=========== starting round " << m_round << " ===========\n";
}

void Arena::print_arena() const {
    std::cout << "    ";
    for (int col = 0; col < m_config.width; ++col) {
        std::cout << std::setw(3) << col;
    }
    std::cout << '\n';

    for (int row = 0; row < m_config.height; ++row) {
        std::cout << std::setw(3) << row << " ";
        for (int col = 0; col < m_config.width; ++col) {
            const char ch = board_cell_symbol(row, col);
            std::cout << std::setw(3) << ch;
        }
        std::cout << '\n';
    }

    for (const auto& entry : m_robots) {
        const char state = entry.alive ? 'R' : 'X';
        std::cout << state << entry.symbol << " " << entry.robot->m_name << " ("
                  << entry.row << "," << entry.col << ") Health: "
                  << entry.robot->get_health() << " Armor: " << entry.robot->get_armor() << '\n';
    }
}

int Arena::count_alive() const {
    int alive = 0;
    for (const auto& robot : m_robots) {
        if (robot.alive) {
            ++alive;
        }
    }
    return alive;
}

std::optional<std::size_t> Arena::winner_index() const {
    std::optional<std::size_t> winner;
    for (std::size_t i = 0; i < m_robots.size(); ++i) {
        if (!m_robots[i].alive) {
            continue;
        }
        if (winner.has_value()) {
            return std::nullopt;
        }
        winner = i;
    }
    return winner;
}

std::vector<std::pair<int, int>> Arena::directional_band_cells(int row, int col, int direction, int max_depth) const {
    std::vector<std::pair<int, int>> cells;

    if (direction == 0) {
        for (int dr = -1; dr <= 1; ++dr) {
            for (int dc = -1; dc <= 1; ++dc) {
                if (dr == 0 && dc == 0) {
                    continue;
                }
                const int nr = row + dr;
                const int nc = col + dc;
                if (in_bounds(nr, nc)) {
                    cells.push_back({nr, nc});
                }
            }
        }
        return cells;
    }

    const int dr = directions[direction].first;
    const int dc = directions[direction].second;
    const int max_steps = (max_depth < 0) ? std::max(m_config.height, m_config.width) : max_depth;

    for (int step = 1; step <= max_steps; ++step) {
        const int center_row = row + dr * step;
        const int center_col = col + dc * step;

        if (!in_bounds(center_row, center_col)) {
            break;
        }

        if (dr == 0) {
            for (int offset = -1; offset <= 1; ++offset) {
                const int rr = center_row + offset;
                const int cc = center_col;
                if (in_bounds(rr, cc)) {
                    cells.push_back({rr, cc});
                }
            }
        } else if (dc == 0) {
            for (int offset = -1; offset <= 1; ++offset) {
                const int rr = center_row;
                const int cc = center_col + offset;
                if (in_bounds(rr, cc)) {
                    cells.push_back({rr, cc});
                }
            }
        } else {
            for (int offset = -1; offset <= 1; ++offset) {
                const int rr = center_row + (offset * -dc);
                const int cc = center_col + (offset * dr);
                if (in_bounds(rr, cc)) {
                    cells.push_back({rr, cc});
                }
            }
        }
    }

    // Keep insertion order but remove duplicates.
    std::set<std::pair<int, int>> seen;
    std::vector<std::pair<int, int>> unique_cells;
    unique_cells.reserve(cells.size());
    for (const auto& c : cells) {
        if (seen.insert(c).second) {
            unique_cells.push_back(c);
        }
    }
    return unique_cells;
}

std::vector<RadarObj> Arena::scan_radar_for_robot(const RobotEntry& entry, int direction) const {
    std::vector<RadarObj> out;
    const auto cells = directional_band_cells(entry.row, entry.col, direction);
    out.reserve(cells.size());

    for (const auto& [row, col] : cells) {
        const RobotEntry* robot = robot_at(row, col);
        if (robot != nullptr) {
            if (robot == &entry) {
                continue;
            }
            out.emplace_back(robot->alive ? 'R' : 'X', row, col);
            continue;
        }

        const Cell& cell = m_cells[static_cast<std::size_t>(row)][static_cast<std::size_t>(col)];
        if (cell.mound) {
            out.emplace_back('M', row, col);
        } else if (cell.pit) {
            out.emplace_back('P', row, col);
        } else if (cell.flame) {
            out.emplace_back('F', row, col);
        }
    }

    return out;
}

bool Arena::take_robot_turn(RobotEntry& entry) {
    int radar_direction = 0;
    entry.robot->get_radar_direction(radar_direction);
    radar_direction = std::clamp(radar_direction, kRadarMinDirection, kRadarMaxDirection);

    const std::vector<RadarObj> radar_results = scan_radar_for_robot(entry, radar_direction);
    entry.robot->process_radar_results(radar_results);

    int shot_row = 0;
    int shot_col = 0;
    const bool wants_to_shoot = entry.robot->get_shot_location(shot_row, shot_col);

    if (wants_to_shoot) {
        const bool shot_handled = handle_shot(entry, shot_row, shot_col);
        if (!shot_handled) {
            std::cout << entry.robot->m_name << " wanted to shoot but could not.\n";
        }
        return shot_handled;
    }

    int move_direction = 0;
    int move_distance = 0;
    entry.robot->get_move_direction(move_direction, move_distance);
    move_direction = std::clamp(move_direction, kMoveMinDirection, kMoveMaxDirection);

    return handle_move(entry, move_direction, move_distance);
}

bool Arena::handle_shot(RobotEntry& shooter, int shot_row, int shot_col) {
    if (!in_bounds(shot_row, shot_col)) {
        shot_row = std::clamp(shot_row, 0, m_config.height - 1);
        shot_col = std::clamp(shot_col, 0, m_config.width - 1);
    }

    std::vector<std::pair<int, int>> impacted_cells;
    const WeaponType weapon = shooter.robot->get_weapon();

    if (weapon == railgun) {
        impacted_cells = ray_to_edge_dda(shooter.row, shooter.col, shot_row, shot_col, m_config.height, m_config.width);
        apply_weapon_damage_to_cells(shooter, impacted_cells);
        return true;
    }

    if (weapon == flamethrower) {
        auto [dr, dc] = normalize_direction_delta(shooter.row, shooter.col, shot_row, shot_col);
        int direction = 0;
        for (int i = 1; i <= 8; ++i) {
            if (directions[i].first == dr && directions[i].second == dc) {
                direction = i;
                break;
            }
        }
        if (direction == 0) {
            return false;
        }

        impacted_cells = directional_band_cells(shooter.row, shooter.col, direction, 4);
        apply_weapon_damage_to_cells(shooter, impacted_cells);
        return true;
    }

    if (weapon == grenade) {
        if (shooter.grenade_shots_fired >= 10 || shooter.robot->get_grenades() <= 0) {
            std::cout << shooter.robot->m_name << " has no grenades remaining.\n";
            return false;
        }
        ++shooter.grenade_shots_fired;
        shooter.robot->decrement_grenades();

        for (int dr = -1; dr <= 1; ++dr) {
            for (int dc = -1; dc <= 1; ++dc) {
                const int rr = shot_row + dr;
                const int cc = shot_col + dc;
                if (in_bounds(rr, cc)) {
                    impacted_cells.push_back({rr, cc});
                }
            }
        }
        apply_weapon_damage_to_cells(shooter, impacted_cells);
        return true;
    }

    if (weapon == hammer) {
        const auto line = ray_to_edge_dda(shooter.row, shooter.col, shot_row, shot_col, m_config.height, m_config.width);
        for (std::size_t i = 0; i < line.size() && i < 2; ++i) {
            impacted_cells.push_back(line[i]);
        }
        apply_weapon_damage_to_cells(shooter, impacted_cells);
        return true;
    }

    return false;
}

void Arena::apply_weapon_damage_to_cells(RobotEntry& shooter, const std::vector<std::pair<int, int>>& impacted_cells) {
    for (const auto& [row, col] : impacted_cells) {
        RobotEntry* target = robot_at(row, col);
        if (!target || !target->alive || target == &shooter) {
            continue;
        }

        const WeaponType weapon = shooter.robot->get_weapon();
        if (weapon == railgun) {
            apply_damage(shooter, *target, 10, 20, "railgun");
        } else if (weapon == flamethrower) {
            apply_damage(shooter, *target, 30, 50, "flamethrower");
        } else if (weapon == grenade) {
            apply_damage(shooter, *target, 10, 40, "grenade");
        } else if (weapon == hammer) {
            apply_damage(shooter, *target, 50, 60, "hammer");
        }
    }
}

void Arena::apply_damage(RobotEntry& attacker, RobotEntry& target, int min_damage, int max_damage, const std::string& cause) {
    const int raw_damage = random_int(min_damage, max_damage);
    const int armor = target.robot->get_armor();
    const int damage = armor_adjusted_damage(raw_damage, armor);

    const int remaining = target.robot->take_damage(damage);
    target.robot->reduce_armor(1);

    std::cout << attacker.robot->m_name << " hits " << target.robot->m_name
              << " with " << cause << " for " << damage << " (raw " << raw_damage
              << ", armor " << armor << ")\n";

    if (remaining <= 0 && target.alive) {
        target.alive = false;
        std::cout << target.robot->m_name << " is out.\n";

        // If a robot dies while standing on flame, that flame is consumed.
        Cell& cell = m_cells[static_cast<std::size_t>(target.row)][static_cast<std::size_t>(target.col)];
        if (cell.flame) {
            cell.flame = false;
        }
    }
}

bool Arena::handle_move(RobotEntry& entry, int move_direction, int move_distance) {
    if (move_direction <= 0 || move_distance <= 0) {
        return false;
    }

    const int max_speed = entry.robot->get_move_speed();
    const int actual_distance = std::min(move_distance, max_speed);
    if (actual_distance <= 0) {
        return false;
    }

    const int dr = directions[move_direction].first;
    const int dc = directions[move_direction].second;
    if (dr == 0 && dc == 0) {
        return false;
    }

    int current_row = entry.row;
    int current_col = entry.col;

    for (int step = 1; step <= actual_distance; ++step) {
        const int next_row = current_row + dr;
        const int next_col = current_col + dc;

        if (!in_bounds(next_row, next_col)) {
            break;
        }

        const RobotEntry* occupant = robot_at(next_row, next_col);
        if (occupant != nullptr) {
            break;
        }

        Cell& next_cell = m_cells[static_cast<std::size_t>(next_row)][static_cast<std::size_t>(next_col)];

        if (next_cell.mound) {
            break;
        }

        // Move one step.
        current_row = next_row;
        current_col = next_col;

        if (next_cell.flame && entry.alive) {
            const int raw_damage = random_int(30, 50);
            const int armor = entry.robot->get_armor();
            const int damage = armor_adjusted_damage(raw_damage, armor);
            const int remaining = entry.robot->take_damage(damage);
            entry.robot->reduce_armor(1);
            std::cout << entry.robot->m_name << " passes through flame for " << damage << " damage.\n";
            if (remaining <= 0) {
                entry.alive = false;
                std::cout << entry.robot->m_name << " died in a flame.\n";
                next_cell.flame = false;
                break;
            }
        }

        if (next_cell.pit) {
            entry.robot->disable_movement();
            std::cout << entry.robot->m_name << " fell into a pit and is trapped.\n";
            break;
        }
    }

    const bool moved = (entry.row != current_row) || (entry.col != current_col);
    entry.row = current_row;
    entry.col = current_col;
    entry.robot->move_to(current_row, current_col);

    if (moved) {
        std::cout << entry.robot->m_name << " moved to (" << current_row << "," << current_col << ")\n";
    }

    return moved;
}

bool Arena::in_bounds(int row, int col) const {
    return row >= 0 && row < m_config.height && col >= 0 && col < m_config.width;
}

bool Arena::cell_has_live_robot(int row, int col) const {
    const RobotEntry* robot = robot_at(row, col);
    return robot != nullptr && robot->alive;
}

bool Arena::cell_has_dead_robot(int row, int col) const {
    const RobotEntry* robot = robot_at(row, col);
    return robot != nullptr && !robot->alive;
}

bool Arena::cell_has_any_robot(int row, int col) const {
    return robot_at(row, col) != nullptr;
}

Arena::RobotEntry* Arena::robot_at(int row, int col) {
    for (auto& robot : m_robots) {
        if (robot.row == row && robot.col == col) {
            return &robot;
        }
    }
    return nullptr;
}

const Arena::RobotEntry* Arena::robot_at(int row, int col) const {
    for (const auto& robot : m_robots) {
        if (robot.row == row && robot.col == col) {
            return &robot;
        }
    }
    return nullptr;
}

char Arena::board_cell_symbol(int row, int col) const {
    const RobotEntry* robot = robot_at(row, col);
    if (robot) {
        return robot->alive ? robot->symbol : 'X';
    }

    const Cell& cell = m_cells[static_cast<std::size_t>(row)][static_cast<std::size_t>(col)];
    if (cell.mound) {
        return 'M';
    }
    if (cell.pit) {
        return 'P';
    }
    if (cell.flame) {
        return 'F';
    }
    return '.';
}

int Arena::random_int(int low, int high) {
    std::uniform_int_distribution<int> dist(low, high);
    return dist(m_rng);
}

void Arena::cleanup() {
    for (auto& entry : m_robots) {
        delete entry.robot;
        entry.robot = nullptr;
        if (entry.handle) {
            dlclose(entry.handle);
            entry.handle = nullptr;
        }
    }
    m_robots.clear();
}

std::string Arena::trim(const std::string& text) {
    std::size_t start = 0;
    while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start]))) {
        ++start;
    }

    std::size_t end = text.size();
    while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1]))) {
        --end;
    }

    return text.substr(start, end - start);
}

std::string Arena::normalize_key(const std::string& key) {
    std::string out;
    out.reserve(key.size());
    for (const unsigned char ch : key) {
        if (std::isspace(ch) || ch == '_') {
            continue;
        }
        out.push_back(static_cast<char>(std::tolower(ch)));
    }
    return out;
}
