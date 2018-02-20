//
// Created by anthony on 12/28/16.
//

#ifndef IRIS_TIMER_H
#define IRIS_TIMER_H

#include <chrono>
#include <string>

class Timer {
public:
  void startTime();
  double endTime(std::string fnName);
  double endTimeWithoutPrint(std::string fnName);
  template<typename Fn>
  bool execute_until(Fn &f,int timeout,std::string args[]){
    this->startTime();
    while(f(args)){
      auto t2 = std::chrono::high_resolution_clock::now();
      auto t =  std::chrono::duration_cast<std::chrono::nanoseconds>(
          t2 - t1).count()/1000000.0;
      if(t>timeout){
        return false;
      }
    }
    return true;
  }
private:
  std::chrono::high_resolution_clock::time_point t1;

};


#endif //IRIS_TIMER_H


