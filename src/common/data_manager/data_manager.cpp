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
#include <iomanip>
#include <labios/common/data_manager/data_manager.h>
#include <labios/common/timer.h>

std::shared_ptr<data_manager> data_manager::instance = nullptr;
/******************************************************************************
 *Interface
 ******************************************************************************/
std::string data_manager::get(const table &name, std::string key,
                              std::string server) {
#ifdef TIMERDM
  Timer t = Timer();
  t.resumeTime();
#endif
  auto return_value = labios_system::getInstance(service_i)->map_client()->get(
      name, key, server);
#ifdef TIMERDM
  std::stringstream stream;
  stream << "data_manager::get()," << std::fixed << std::setprecision(10)
         << t.pauseTime() << "\n";
  std::cout << stream.str();
#endif
  return return_value;
}

int data_manager::put(const table &name, std::string key, std::string data,
                      std::string server) {
#ifdef TIMERDM
  Timer t = Timer();
  t.resumeTime();
#endif
  auto return_value = labios_system::getInstance(service_i)->map_client()->put(
      name, key, data, server);
#ifdef TIMERDM
  std::stringstream stream;
  stream << "data_manager::put()," << std::fixed << std::setprecision(10)
         << t.pauseTime() << "\n";
  std::cout << stream.str();
#endif
  return return_value;
}

bool data_manager::exists(const table &name, std::string key,
                          std::string server) {
  return labios_system::getInstance(service_i)->map_client()->exists(name, key,
                                                                     server);
}

std::string data_manager::remove(const table &name, std::string key,
                                 std::string server) {
#ifdef TIMERDM
  Timer t = Timer();
  t.resumeTime();
#endif
  auto return_value =
      labios_system::getInstance(service_i)->map_client()->remove(
          name, std::move(key), std::move(server));
#ifdef TIMERDM
  std::stringstream stream;
  stream << "data_manager::remove()," << std::fixed << std::setprecision(10)
         << t.pauseTime() << "\n";
  std::cout << stream.str();
#endif
  return return_value;
}
