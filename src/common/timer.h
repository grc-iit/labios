//
// Created by hdevarajan on 5/9/18.
//

#ifndef AETRIO_TIMER_H
#define AETRIO_TIMER_H

#include <chrono>
#include <string>

class Timer {
public:
    Timer():elapsed_time(0){}
    void startTime();
    double endTimeWithPrint(std::string fnName);
    double stopTime();
    double pauseTime();
    void resumeTime();
    double elapsed_time;
private:
    std::chrono::high_resolution_clock::time_point t1;

};

#endif //AETRIO_TIMER_H
