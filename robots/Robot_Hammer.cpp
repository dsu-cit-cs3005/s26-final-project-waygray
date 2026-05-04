// Robot_Hammer.cpp — hammer weapon robot
// Strategy: scans surroundings (dir 0), rushes toward nearest enemy and smashes.
#include "RobotBase.h"
#include <cmath>
#include <limits>
#include <vector>

class Robot_Hammer : public RobotBase {
public:
    Robot_Hammer() : RobotBase(4, 3, hammer) {
        m_name = "Hammerhead";
    }

    void get_radar_direction(int& dir) override {
        dir = 0; // scan all 8 adjacent cells every turn
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
        int my_row = 0, my_col = 0;
        get_current_location(my_row, my_col);
        int d = std::abs(m_target_row - my_row) + std::abs(m_target_col - my_col);
        if (d <= 2) {
            row = m_target_row;
            col = m_target_col;
            m_has_target = false;
            return true;
        }
        return false;
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
            dist = get_move_speed();
        } else {
            // Zigzag patrol
            dir = m_patrol_right ? 3 : 7;
            m_patrol_right = !m_patrol_right;
            dist = get_move_speed();
        }
    }

private:
    bool m_has_target = false;
    int m_target_row = -1;
    int m_target_col = -1;
    bool m_patrol_right = true;
};

extern "C" RobotBase* create_robot() { return new Robot_Hammer(); }
extern "C" const char* robot_summary() { return "Scans adj, rushes in, smashes close targets."; }
