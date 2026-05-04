#include "Arena.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace {
std::string trim(const std::string& text) {
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

std::string normalize_key(const std::string& key) {
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

bool parse_bool(const std::string& raw, bool& out) {
    const std::string value = normalize_key(trim(raw));
    if (value == "true" || value == "1" || value == "yes") {
        out = true;
        return true;
    }
    if (value == "false" || value == "0" || value == "no") {
        out = false;
        return true;
    }
    return false;
}

bool load_config(const std::string& filename, ArenaConfig& config) {
    std::ifstream in(filename);
    if (!in) {
        std::cerr << "Unable to open config file: " << filename << "\n";
        return false;
    }

    std::string line;
    int parsed_fields = 0;

    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') {
            continue;
        }

        const std::size_t colon_pos = line.find(':');
        if (colon_pos == std::string::npos) {
            continue;
        }

        const std::string key = normalize_key(line.substr(0, colon_pos));
        const std::string value = trim(line.substr(colon_pos + 1));

        try {
        if (key == "arenasize") {
            std::istringstream iss(value);
            int height = 0;
            int width = 0;
            if (!(iss >> height >> width)) {
                std::cerr << "Invalid Arena_Size line. Expected: Arena_Size:<height> <width>\n";
                return false;
            }
            config.height = height;
            config.width = width;
            ++parsed_fields;
        } else if (key == "maxrounds") {
            config.max_rounds = std::stoi(value);
            ++parsed_fields;
        } else if (key == "sleepinterval") {
            config.sleep_interval_seconds = std::stod(value);
            ++parsed_fields;
        } else if (key == "gamestatelive") {
            bool live = false;
            if (!parse_bool(value, live)) {
                std::cerr << "Invalid Game_State_Live value: " << value << "\n";
                return false;
            }
            config.game_state_live = live;
            ++parsed_fields;
        } else if (key == "flamethrowers") {
            config.flamethrowers = std::stoi(value);
            ++parsed_fields;
        } else if (key == "pits") {
            config.pits = std::stoi(value);
            ++parsed_fields;
        } else if (key == "mounds") {
            config.mounds = std::stoi(value);
            ++parsed_fields;
        }
        } catch (const std::exception&) {
            std::cerr << "Invalid numeric value for key: " << key << "\n";
            return false;
        }
    }

    if (config.height <= 0 || config.width <= 0) {
        std::cerr << "Arena dimensions must be positive.\n";
        return false;
    }
    if (config.max_rounds <= 0) {
        std::cerr << "Max_Rounds must be positive.\n";
        return false;
    }
    if (config.sleep_interval_seconds < 0.0) {
        std::cerr << "Sleep_interval must be >= 0.\n";
        return false;
    }

    config.flamethrowers = std::max(0, config.flamethrowers);
    config.pits = std::max(0, config.pits);
    config.mounds = std::max(0, config.mounds);

    if (parsed_fields == 0) {
        std::cerr << "No valid settings were parsed from config file.\n";
        return false;
    }

    return true;
}
} // namespace

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <config.txt>\n";
        return 1;
    }

    ArenaConfig config;
    if (!load_config(argv[1], config)) {
        return 1;
    }

    std::cout << "Starting RobotWarz with config:\n";
    std::cout << "Arena_Size: " << config.height << " " << config.width << "\n";
    std::cout << "Max_Rounds: " << config.max_rounds << "\n";
    std::cout << "Sleep_interval: " << config.sleep_interval_seconds << "\n";
    std::cout << "Game_State_Live: " << (config.game_state_live ? "true" : "false") << "\n";
    std::cout << "Flamethrowers: " << config.flamethrowers << "\n";
    std::cout << "Pits: " << config.pits << "\n";
    std::cout << "Mounds: " << config.mounds << "\n";

    Arena arena(config);
    if (!arena.initialize()) {
        return 1;
    }
    arena.run();

    return 0;
}
