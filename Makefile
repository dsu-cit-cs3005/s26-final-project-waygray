# Compiler
CXX = g++
CXXFLAGS = -std=c++20 -Wall -Wextra -pedantic

# Targets
all: test_robot RobotWarz

RobotBase.o: RobotBase.cpp RobotBase.h
	$(CXX) $(CXXFLAGS) -c RobotBase.cpp

test_robot: test_robot.cpp RobotBase.o
	$(CXX) $(CXXFLAGS) test_robot.cpp RobotBase.o -ldl -o test_robot

RobotWarz: RobotWarz.cpp Arena.cpp Arena.h RobotBase.o RadarObj.h
	$(CXX) $(CXXFLAGS) RobotWarz.cpp Arena.cpp RobotBase.o -ldl -o RobotWarz

clean:
	rm -f *.o test_robot RobotWarz *.so
