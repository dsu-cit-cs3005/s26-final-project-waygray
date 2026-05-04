#include "RobotBase.h"

#include <cmath>
#include <limits>
#include <vector>

class Robot_Waygray : public RobotBase {
public:
    Robot_Waygray() : RobotBase(3, 4, railgun) {
        m_name = "Waygray";
    }

    void get_radar_direction(int& radar_direction) override {
        // Alternate horizontal sweeps to improve chance of finding targets.
        radar_direction = m_scan_right ? 3 : 7;
        m_scan_right = !m_scan_right;
    }

    void process_radar_results(const std::vector<RadarObj>& radar_results) override {
        m_has_target = false;
        int current_row = 0;
        int current_col = 0;
        get_current_location(current_row, current_col);

        int best_dist = std::numeric_limits<int>::max();
        for (const auto& obj : radar_results) {
            if (obj.m_type != 'R') {
                continue;
            }
            const int dist = std::abs(obj.m_row - current_row) + std::abs(obj.m_col - current_col);
            if (dist < best_dist) {
                best_dist = dist;
                m_target_row = obj.m_row;
                m_target_col = obj.m_col;
                m_has_target = true;
            }
        }
    }

    bool get_shot_location(int& shot_row, int& shot_col) override {
        if (!m_has_target) {
            return false;
        }

        shot_row = m_target_row;
        shot_col = m_target_col;
        m_has_target = false;
        return true;
    }

    void get_move_direction(int& direction, int& distance) override {
        int current_row = 0;
        int current_col = 0;
        get_current_location(current_row, current_col);

        // Patrol diagonally between corners if there is no immediate shot.
        const int target_row = m_move_down ? (m_board_row_max - 1) : 0;
        const int target_col = m_move_right ? (m_board_col_max - 1) : 0;

        const int dr = (target_row > current_row) ? 1 : ((target_row < current_row) ? -1 : 0);
        const int dc = (target_col > current_col) ? 1 : ((target_col < current_col) ? -1 : 0);

        if (dr == 0 && dc == 0) {
            if (m_move_down) {
                m_move_down = false;
            } else {
                m_move_down = true;
                m_move_right = !m_move_right;
            }
        }

        if (dr == -1 && dc == 0) direction = 1;
        else if (dr == -1 && dc == 1) direction = 2;
        else if (dr == 0 && dc == 1) direction = 3;
        else if (dr == 1 && dc == 1) direction = 4;
        else if (dr == 1 && dc == 0) direction = 5;
        else if (dr == 1 && dc == -1) direction = 6;
        else if (dr == 0 && dc == -1) direction = 7;
        else if (dr == -1 && dc == -1) direction = 8;
        else direction = 0;

        distance = (direction == 0) ? 0 : 1;
    }

private:
    bool m_scan_right = true;
    bool m_has_target = false;
    int m_target_row = -1;
    int m_target_col = -1;

    bool m_move_down = true;
    bool m_move_right = true;
};

extern "C" RobotBase* create_robot() {
    return new Robot_Waygray();
}

extern "C" const char* robot_summary() {
    return "Scans horiz, snipes with railgun, diagonal patrol.";
}
