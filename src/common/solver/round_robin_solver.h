//
// Created by hdevarajan on 5/19/18.
//

#ifndef AETRIO_MAIN_ROUND_ROBIN_SOLVER_H
#define AETRIO_MAIN_ROUND_ROBIN_SOLVER_H


#include "../data_structures.h"
#include "solver.h"

class round_robin_solver:public solver {
private:
    static std::shared_ptr<round_robin_solver> instance;
    std::size_t last_worker_index;
    explicit round_robin_solver(service service):solver(service),
            last_worker_index(0){}
public:
    inline static std::shared_ptr<round_robin_solver> getInstance(service service){
        return instance == nullptr ? instance=
                std::shared_ptr<round_robin_solver>
                        (new round_robin_solver(service)) : instance;
    }

    solver_output_dp solve(solver_input_dp input) override;
};


#endif //AETRIO_MAIN_ROUND_ROBIN_SOLVER_H
