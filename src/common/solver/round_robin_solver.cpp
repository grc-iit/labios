/*
 * Copyright (C) 2019  SCS Lab <scs-help@cs.iit.edu>, Hariharan
 * Devarajan <hdevarajan@hawk.iit.edu>, Anthony Kougkas
 * <akougkas@iit.edu>, Xian-He Sun <sun@iit.edu>
 *
 * This file is part of Labios
 *
 * Labios is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program.  If not, see
 * <http://www.gnu.org/licenses/>.
 */
/******************************************************************************
 *include files
 ******************************************************************************/
#include <labios/common/solver/round_robin_solver.h>
#include <labios/labios_system.h>

std::shared_ptr<round_robin_solver> round_robin_solver::instance = nullptr;
/******************************************************************************
 *Interface
 ******************************************************************************/
solver_output round_robin_solver::solve(solver_input input) {
  std::vector<task *> worker_tasks;
  auto map_server = labios_system::getInstance(service_i)->map_server();

  solver_output solution(input.num_tasks);
  for (auto i = 0; i < input.tasks.size(); i++) {
    std::size_t worker_id = map_server->counter_inc(
        COUNTER_DB, ROUND_ROBIN_INDEX, std::to_string(-1));
    worker_id = worker_id % MAX_WORKER_COUNT;
    switch (input.tasks[i]->t_type) {
    case task_type::WRITE_TASK: {
      auto *wt = reinterpret_cast<write_task *>(input.tasks[i]);
      if (wt->destination.worker == -1)
        solution.solution[i] = static_cast<int>(worker_id + 1);
      else
        solution.solution[i] = wt->destination.worker;
      break;
    }
    case task_type::READ_TASK: {
      auto *rt = reinterpret_cast<read_task *>(input.tasks[i]);
      if (rt->source.worker == -1)
        solution.solution[i] = static_cast<int>(worker_id + 1);
      else
        solution.solution[i] = rt->source.worker;
      break;
    }
    case task_type::DELETE_TASK: {
      auto *dt = reinterpret_cast<delete_task *>(input.tasks[i]);
      if (dt->source.worker == -1)
        std::cerr << "round_robin_solver::solve():\t "
                     "delete task failed\n";
      else
        solution.solution[i] = dt->source.worker;
      break;
    }
    case task_type::FLUSH_TASK: {
      auto *ft = reinterpret_cast<flush_task *>(input.tasks[i]);
      if (ft->source.worker == -1)
        std::cerr << "round_robin_solver::solve():\t "
                     "flush task failed\n";
      else
        solution.solution[i] = ft->source.worker;
      break;
    }
    default:
      std::cerr << "round_robin_solver::solve()\t "
                   "task type invalid\n";
    }
#ifdef DEBUG
    std::cout << "Task#" << i << " Worker#" << solution.solution[i] << "\n";
#endif
    auto it = solution.worker_task_map.find(solution.solution[i]);
    if (it == solution.worker_task_map.end()) {
      worker_tasks = std::vector<task *>();
      worker_tasks.push_back(input.tasks[i]);
      solution.worker_task_map.emplace(
          std::make_pair(solution.solution[i], worker_tasks));
    } else
      it->second.push_back(input.tasks[i]);
  }
  return solution;
}
