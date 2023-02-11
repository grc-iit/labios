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
 * Created by hariharan on 2/16/18.
 * Updated by akougkas on 6/26/2018
 ******************************************************************************/
#ifndef LABIOS_MAIN_POSIX_H
#define LABIOS_MAIN_POSIX_H
/******************************************************************************
 *include files
 ******************************************************************************/
#include <cstdio>
#include <cstring>
#include <labios/common/data_manager/data_manager.h>
#include <labios/common/metadata_manager/metadata_manager.h>
#include <labios/drivers/mpi.h>
#include <labios/labios_system.h>
/******************************************************************************
 *Labios Namespace
 ******************************************************************************/
namespace labios {
/******************************************************************************
 *Interface
 ******************************************************************************/
FILE *fopen(const char *filename, const char *mode);
int fclose(FILE *stream);
int fseek(FILE *stream, long int offset, int origin);
size_t fread(void *ptr, size_t size, size_t count, FILE *stream);
std::vector<read_task> fread_async(size_t size, size_t count, FILE *stream);
std::size_t fread_wait(void *ptr, std::vector<read_task> &tasks,
                       std::string filename);
std::vector<write_task *> fwrite_async(void *ptr, size_t size, size_t count,
                                       FILE *stream);
size_t fwrite_wait(std::vector<write_task *> tasks);
size_t fwrite(void *ptr, size_t size, size_t count, FILE *stream);
} // namespace labios

#endif // LABIOS_MAIN_POSIX_H
