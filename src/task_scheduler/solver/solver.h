//
// Created by hdevarajan on 5/8/18.
//

#ifndef PORUS_MAIN_SOLVER_H
#define PORUS_MAIN_SOLVER_H
template <typename R,typename I>
class solver {
public:
    virtual R solve(I input)=0;
};
#endif //PORUS_MAIN_SOLVER_H
