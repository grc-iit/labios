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
 * Created by hariharan on 5/9/18.
 * Updated by akougkas on 6/26/2018
 ******************************************************************************/
/******************************************************************************
 *include files
 ******************************************************************************/
#include "task_scheduler.h"
#include <labios/common/data_structures.h>
#include <labios/common/utilities.h>
#include <mpi.h>
/******************************************************************************
 *Main
 ******************************************************************************/
int main(int argc, char **argv) {
  MPI_Init(&argc, &argv);
  ConfigManager::get_instance()->LoadConfig(argv[1]);
  auto scheduler_service = task_scheduler::getInstance(TASK_SCHEDULER);
  scheduler_service->run();
  MPI_Finalize();
  return 0;
}
