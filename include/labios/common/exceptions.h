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
// Created by hariharan on 3/3/18.
//

#ifndef LABIOS_MAIN_EXCEPTION_H
#define LABIOS_MAIN_EXCEPTION_H

#include <stdexcept>

class NotImplementedException : public std::logic_error {
public:
  NotImplementedException(const std::string &__arg) : logic_error(__arg) {}

  virtual char const *what() const noexcept override {
    return "Function not yet implemented.";
  }
};
#endif // LABIOS_MAIN_EXCEPTION_H
