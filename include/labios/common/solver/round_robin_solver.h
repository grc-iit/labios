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
/*******************************************************************************
 * Created by hariharan on 5/19/18.
 * Updated by akougkas on 6/30/2018
 ******************************************************************************/
#ifndef LABIOS_MAIN_ROUND_ROBIN_SOLVER_H
#define LABIOS_MAIN_ROUND_ROBIN_SOLVER_H
/******************************************************************************
 *include files
 ******************************************************************************/
#include <labios/common/data_structures.h>
#include <labios/common/solver/solver.h>
/******************************************************************************
 *Class
 ******************************************************************************/
class round_robin_solver : public solver {
private:
  /******************************************************************************
   *Variables and members
   ******************************************************************************/
  static std::shared_ptr<round_robin_solver> instance;
  /******************************************************************************
   *Constructor
   ******************************************************************************/
  explicit round_robin_solver(service service) : solver(service) {}

public:
  /******************************************************************************
   *Interface
   ******************************************************************************/
  inline static std::shared_ptr<round_robin_solver>
  getInstance(service service) {
    return instance == nullptr ? instance = std::shared_ptr<round_robin_solver>(
                                     new round_robin_solver(service))
                               : instance;
  }
  solver_output solve(solver_input input) override;
};

#endif // LABIOS_MAIN_ROUND_ROBIN_SOLVER_H
