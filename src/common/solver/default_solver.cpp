/******************************************************************************
*include files
******************************************************************************/
#include "default_solver.h"
#include "../../worker_manager/worker_manager_service.h"

default_solver::default_solver(service service) : solver(service) {

}
/******************************************************************************
*Interface
******************************************************************************/
solver_output_dp default_solver::solve(solver_input_dp input) {


    return solver_output_dp(0);
}
