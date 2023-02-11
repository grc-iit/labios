//
// Created by lukemartinlogan on 7/22/22.
//

#ifndef LABIOS_BENCH_UTIL_H
#define LABIOS_BENCH_UTIL_H

#include "labios/common/return_codes.h"
#include "labios/common/threadPool.h"
#include "labios/common/timer.h"
#include "labios/common/utilities.h"
#include "labios/drivers/posix.h"
#include <fcntl.h>
#include <fstream>
#include <iomanip>
#include <malloc.h>
#include <mpi.h>
#include <random>
#include <sstream>
#include <zconf.h>

static void gen_random(char *s, std::size_t len) {
  static const char alphanum[] = "0123456789"
                                 "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                 "abcdefghijklmnopqrstuvwxyz";
  std::default_random_engine generator;
  std::uniform_int_distribution<int> dist(1, 1000000);
  for (int i = 0; i < len; ++i) {
    s[i] = alphanum[dist(generator) % (sizeof(alphanum) - 1)];
  }

  s[len] = 0;
}

static float get_average_ts() {
  /* run system command du -s to calculate size of director. */
  std::string cmd = "sh /home/cc/nfs/aetrio/scripts/calc_ts.sh";
  FILE *fp;
  std::array<char, 128> buffer;
  std::string result;
  std::shared_ptr<FILE> pipe(popen(cmd.c_str(), "r"), pclose);
  if (!pipe)
    throw std::runtime_error("popen() failed!");
  while (!feof(pipe.get())) {
    if (fgets(buffer.data(), 128, pipe.get()) != nullptr)
      result += buffer.data();
  }
  return std::stof(result);
}

static float get_average_worker() {
  /* run system command du -s to calculate size of director. */
  std::string cmd = "sh /home/cc/nfs/aetrio/scripts/calc_worker.sh";
  FILE *fp;
  std::array<char, 128> buffer;
  std::string result;
  std::shared_ptr<FILE> pipe(popen(cmd.c_str(), "r"), pclose);
  if (!pipe)
    throw std::runtime_error("popen() failed!");
  while (!feof(pipe.get())) {
    if (fgets(buffer.data(), 128, pipe.get()) != nullptr)
      result += buffer.data();
  }
  return std::stof(result);
}

static void wait_for_read(size_t size, std::vector<read_task> tasks,
                          std::string filename) {
  char read_buf[size];
  auto bytes = labios::fread_wait(read_buf, tasks, filename);
  if (bytes != size)
    std::cerr << "Read failed:" << bytes << "\n";
}

#endif // LABIOS_BENCH_UTIL_H
