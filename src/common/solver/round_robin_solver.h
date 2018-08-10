/*******************************************************************************
* Created by hariharan on 5/19/18.
* Updated by akougkas on 6/30/2018
* Illinois Institute of Technology - SCS Lab
* (C) 2018
******************************************************************************/
#ifndef AETRIO_MAIN_ROUND_ROBIN_SOLVER_H
#define AETRIO_MAIN_ROUND_ROBIN_SOLVER_H
/******************************************************************************
*include files
******************************************************************************/
#include "../data_structures.h"
#include "solver.h"
/******************************************************************************
*Class
******************************************************************************/
class round_robin_solver:public solver {
private:
/******************************************************************************
*Variables and members
******************************************************************************/
    static std::shared_ptr<round_robin_solver> instance;
/******************************************************************************
*Constructor
******************************************************************************/
    explicit round_robin_solver(service service):solver(service){}
public:
/******************************************************************************
*Interface
******************************************************************************/
    inline static std::shared_ptr<round_robin_solver> getInstance(service service){
        return instance == nullptr ? instance=
                std::shared_ptr<round_robin_solver>
                        (new round_robin_solver(service)) : instance;
    }
    solver_output solve(solver_input input) override;
};


#endif //AETRIO_MAIN_ROUND_ROBIN_SOLVER_H
