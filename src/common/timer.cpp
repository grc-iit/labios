//
// Created by hdevarajan on 5/9/18.
//

#include "timer.h"
#include <iostream>
#include <iomanip>

void Timer::startTime() {
    t1 = std::chrono::high_resolution_clock::now();
}

double Timer::endTime(std::string fnName) {
    auto t2 = std::chrono::high_resolution_clock::now();
    auto t =  std::chrono::duration_cast<std::chrono::nanoseconds>(
            t2 - t1).count()/1000000000.0;
    if( t > 0.001){
        printf("%s : %lf\n",fnName.c_str(),t);
    }
    return t;
}

double Timer::endTimeWithoutPrint(std::string fnName) {
    auto t2 = std::chrono::high_resolution_clock::now();
    auto t =  std::chrono::duration_cast<std::chrono::nanoseconds>(
            t2 - t1).count()/1000000000.0;
    return t;
}