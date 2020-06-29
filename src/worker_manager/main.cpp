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
#include <iostream>
#include <mpi.h>
#include "worker_manager_service.h"
#include <labios/common/utilities.h>

int main(int argc, char** argv) {
    parse_opts(argc,argv);
    MPI_Init(&argc,&argv);
    std::shared_ptr<worker_manager_service>
            worker_manager_service_i=worker_manager_service::getInstance
                    (service::WORKER_MANAGER);
    worker_manager_service_i->run();
    MPI_Finalize();
    return 0;
}
