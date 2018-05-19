//
// Created by hdevarajan on 5/19/18.
//

#ifndef PORUS_MAIN_ROUND_ROBIN_SOLVER_H
#define PORUS_MAIN_ROUND_ROBIN_SOLVER_H


#include "../data_structures.h"
#include "solver.h"

class round_robin_solver:public solver {
private:
    static std::shared_ptr<round_robin_solver> instance;
    int last_worker_index;
    round_robin_solver(service service_1):solver(service_1),last_worker_index(0){}
public:
    inline static std::shared_ptr<round_robin_solver> getInstance(service service_1){
        return instance == nullptr ? instance=std::shared_ptr<round_robin_solver>(new round_robin_solver(service_1))
                                  : instance;
    }

    solver_output solve(solver_input input) override ;
};


#endif //PORUS_MAIN_ROUND_ROBIN_SOLVER_H
