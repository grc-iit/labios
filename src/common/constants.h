//
// Created by anthony on 5/11/17.
//

#ifndef PORUS_CONSTANTS_H
#define PORUS_CONSTANTS_H
#include <string>
/******************************************************************************
*Request Handler
******************************************************************************/
static const std::string RH_TWO_SIDED = "RH_TWO_SIDED";
static const std::string RH_ONE_SIDED = "RH_ONE_SIDED";
static const std::string RH_GLOBAL_SIDED = "RH_GLOBAL_SIDED";
static const std::string RH_APP_SIDED = "RH_APP_SIDED";
static const std::size_t MAX_REQUEST_SIZE = 64*1024*1024;
/******************************************************************************
*Porus library parameters
******************************************************************************/
static const u_int16_t MAX_FILENAME_LENGTH = 256;
static const u_int32_t TIMEOUT = 5000;
static const size_t MAX_REQUEST_QUEUE_SIZE = 100*1000;
const std::string SEPARATOR = "#";
/******************************************************************************
*Metadata Manager
******************************************************************************/
static const std::string POSIX_MDM = "POSIX_MDM";
/******************************************************************************
*TASK HANDLER
******************************************************************************/
static const std::string SYNC_TH = "SYNC_TH";
static const std::string ASYNC_TH = "ASYNC_TH";
/******************************************************************************
*TASK SCHEDULER
******************************************************************************/
static const std::string ACTIVE_WORKER_TS = "ACTIVE_WORKER_TS";
static const std::size_t BASE_SIZE_OF_BUCKET = 8192;
static const int TOTAL_NUM_OF_BUCKETS = 16;
static const std::string SCHEDULER_LEADER="SCHEDULER_LEADER";
/******************************************************************************
*Keyspaces
******************************************************************************/
const std::string KEY_SPACE_SEPARATOR="";
static const std::string DATA_SPACE = "DS";
const std::string TASKQUEUE="T";
const std::string READING="R";
const std::string WRITING="W";
const std::string COMPLETE="C";
const std::string LEADER="L";
const std::string WORKER_STATUS="W"+KEY_SPACE_SEPARATOR+"S";
const std::string READ_TASK_QUEUE=READING+KEY_SPACE_SEPARATOR+TASKQUEUE;
const std::string WRITE_TASK_QUEUE=WRITING+KEY_SPACE_SEPARATOR+TASKQUEUE;
const std::string COMPLETE_TASK=COMPLETE+KEY_SPACE_SEPARATOR+TASKQUEUE;
#endif //PORUS_CONSTANTS_H
