//
// Created by hdevarajan on 5/9/18.
//

#ifndef AETRIO_TIMER_H
#define AETRIO_TIMER_H

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

#endif //AETRIO_TIMER_H
