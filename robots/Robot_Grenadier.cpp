// Robot_Grenadier.cpp — grenade launcher robot
// Strategy: scans in expanding spiral, lobs grenades at any enemy spotted.
#include "RobotBase.h"
#include <cmath>
#include <limits>
#include <vector>

class Robot_Grenadier : public RobotBase {
public:
    Robot_Grenadier() : RobotBase(3, 4, grenade) {
        m_name = "Grenadier";
    }

    void get_radar_direction(int& dir) override {
        // Cycle through all 8 directions then back to surroundings scan
        dir = m_radar_dir;
        m_radar_dir = (m_radar_dir % 8) + 1;
    }

    void process_radar_results(const std::vector<RadarObj>& results) override {
        m_has_target = false;
        int my_row = 0, my_col = 0;
        get_current_location(my_row, my_col);
        int best = std::numeric_limits<int>::max();
        for (const auto& obj : results) {
            if (obj.m_type != 'R') continue;
            int d = std::abs(obj.m_row - my_row) + std::abs(obj.m_col - my_col);
            if (d < best) {
                best = d;
                m_target_row = obj.m_row;
                m_target_col = obj.m_col;
                m_has_target = true;
            }
        }
    }

    bool get_shot_location(int& row, int& col) override {
        if (!m_has_target) return false;
        if (get_grenades() <= 0) return false;
        row = m_target_row;
        col = m_target_col;
        m_has_target = false;
        return true;
    }

    void get_move_direction(int& dir, int& dist) override {
        int my_row = 0, my_col = 0;
        get_current_location(my_row, my_col);
        if (m_has_target) {
            int dr = m_target_row - my_row;
            int dc = m_target_col - my_col;
            int ndr = (dr > 0) ? 1 : (dr < 0 ? -1 : 0);
            int ndc = (dc > 0) ? 1 : (dc < 0 ? -1 : 0);
            dir = 0;
            for (int i = 1; i <= 8; ++i) {
                if (directions[i].first == ndr && directions[i].second == ndc) {
                    dir = i; break;
                }
            }
            dist = 1;
        } else {
            // Move diagonally toward center; fall back to patrol if already there
            int center_row = m_board_row_max / 2;
            int center_col = m_board_col_max / 2;
            int dr = (center_row > my_row) ? 1 : (center_row < my_row ? -1 : 0);
            int dc = (center_col > my_col) ? 1 : (center_col < my_col ? -1 : 0);
            if (dr == 0 && dc == 0) {
                // Already at center — patrol in expanding spiral direction
                dr = 1; dc = 1;
            }
            dir = 0;
            for (int i = 1; i <= 8; ++i) {
                if (directions[i].first == dr && directions[i].second == dc) {
                    dir = i; break;
                }
            }
            dist = get_move_speed();
        }
    }

private:
    int m_radar_dir = 1;
    bool m_has_target = false;
    int m_target_row = -1;
    int m_target_col = -1;
};

extern "C" RobotBase* create_robot() { return new Robot_Grenadier(); }
extern "C" const char* robot_summary() { return "Spiral scan, lobs grenades, moves toward center."; }
