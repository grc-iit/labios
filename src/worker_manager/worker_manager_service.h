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
//
// Created by hdevarajan on 5/14/18.
//

#ifndef LABIOS_MAIN_WORKER_MANAGER_SERVICE_H
#define LABIOS_MAIN_WORKER_MANAGER_SERVICE_H

#include <labios/common/enumerations.h>
#include <memory>

class worker_manager_service {
  static std::shared_ptr<worker_manager_service> instance;
  service service_i;
  worker_manager_service(service service) : service_i(service), kill(false) {}

  int sort_worker_score();

public:
  int kill;
  inline static std::shared_ptr<worker_manager_service>
  getInstance(service service) {
    return instance == nullptr
               ? instance = std::shared_ptr<worker_manager_service>(
                     new worker_manager_service(service))
               : instance;
  }
  void run();
};

#endif // LABIOS_MAIN_WORKER_MANAGER_SERVICE_H
