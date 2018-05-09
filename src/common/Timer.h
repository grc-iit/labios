//
// Created by hdevarajan on 5/9/18.
//

#ifndef RDSA_TIMER_H
#define RDSA_TIMER_H

#include <chrono>
#include <string>

class Timer {
public:
    void startTime();
    double endTime(std::string fnName);
    double endTimeWithoutPrint(std::string fnName);
private:
    std::chrono::high_resolution_clock::time_point t1;
};

#endif //RDSA_TIMER_H
