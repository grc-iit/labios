/*******************************************************************************
* Created by hariharan on 2/16/18.
* Updated by akougkas on 6/26/2018
* Illinois Institute of Technology - SCS Lab
* (C) 2018
******************************************************************************/
#ifndef AETRIO_MAIN_CONSTANTS_H
#define AETRIO_MAIN_CONSTANTS_H
/******************************************************************************
*include files
******************************************************************************/
#include <cstddef>
#include <climits>
#include <string>
/******************************************************************************
*Aetrio parameters
******************************************************************************/
const std::string AETRIO_CLIENT_PORT = "9999";
const size_t KEY_SIZE = 256;
const size_t FILE_SIZE = 256;
const long long MAX_DATA_SIZE = 2 * 1024 * 1024 * 1024;
const size_t CHUNK_SIZE = 2 * 1024 * 1024;
const long long MAX_MESSAGE_SIZE = LLONG_MAX;
const std::string ALL_KEYS = "ALL";
const std::string kDBPath_client = "/tmp/rocksdb";
const std::string kDBPath_server = "/tmp/rocksdb";
const std::size_t MAX_IO_UNIT = 1 * 1024 * 1024;
const std::string CLIENT_TASK_SUBJECT = "TASK";
/*******************
*Configs
*******************/
const map_impl_type map_impl_type_t = map_impl_type::MEMCACHE_D;
const solver_impl_type solver_impl_type_t = solver_impl_type::ROUND_ROBIN;
const queue_impl_type queue_impl_type_t = queue_impl_type::NATS;
const io_client_type io_client_type_t = io_client_type::POSIX;
const std::string DATASPACE_ID="DATASPACE_ID";
const std::string KEY_SEPARATOR = "#";
const size_t PROCS_PER_MEMCACHED=24;
/*******************
*Workers
*******************/
const std::size_t MAX_WORKER_COUNT = 4;
const int WORKER_SPEED =2;
const int WORKER_ENERGY=2;
const int64_t WORKER_CAPACITY_MAX=137438953472;
const std::string WORKER_PATH="/opt/temp/";
const std::string PFS_PATH="/mnt/pfs";
const size_t KB = 1024;
const std::size_t WORKER_ATTRIBUTES_COUNT=5;
const float POLICY_WEIGHT[WORKER_ATTRIBUTES_COUNT] = {.3,.2,.3,.1,.1};
const double WORKER_INTERVAL=2.0;
const std::size_t MAX_WORKER_TASK_COUNT = 50;
/*******************
*Scheduler
*******************/
const std::size_t MAX_NUM_TASKS_IN_QUEUE=1;
const std::size_t MAX_SCHEDULE_TIMER=1;
const std::size_t MAX_READ_TIMER=3;
const std::size_t MAX_TASK_TIMER_MS=MAX_SCHEDULE_TIMER*1000;
const std::size_t MAX_TASK_TIMER_MS_MAX=MAX_SCHEDULE_TIMER*1000000;
const std::size_t WORKER_MANAGER_INTERVAL=5;
const std::size_t SYSTEM_MANAGER_INTERVAL=5;

#endif //AETRIO_MAIN_CONSTANTS_H
