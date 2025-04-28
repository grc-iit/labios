/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * Distributed under BSD 3-Clause license.                                   *
 * Copyright by The HDF Group.                                               *
 * Copyright by the Illinois Institute of Technology.                        *
 * All rights reserved.                                                      *
 *                                                                           *
 * This file is part of Hermes. The full Hermes copyright notice, including  *
 * terms governing use, modification, and redistribution, is contained in    *
 * the COPYING file, which can be found at the top directory. If you do not  *
 * have access to the file, you may request a copy from help@hdfgroup.org.   *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#include "labios/labios_client.h"
#include "chimaera/api/chimaera_runtime.h"
#include "chimaera/monitor/monitor.h"
#include "chimaera_admin/chimaera_admin_client.h"
#include "mytokenizer.cpp"

namespace chi::labios {

//   std::string mytokenizer(char* data, size_t data_size) {
//   // For testing purposes, just return a copy of the data
//   return std::string(data, data_size);
// }

class Server : public Module {
 public:
  CLS_CONST LaneGroupId kDefaultGroup = 0;
  std::unordered_map<std::string, std::string> token_map_;

 public:
  Server() = default;

  CHI_BEGIN(Create)
  /** Construct labios */
  void Create(CreateTask *task, RunContext &rctx) {
    // Create a set of lanes for holding tasks
    CreateLaneGroup(kDefaultGroup, 1, QUEUE_LOW_LATENCY);
  }
  void MonitorCreate(MonitorModeId mode, CreateTask *task, RunContext &rctx) {}
  CHI_END(Create)

  /** Route a task to a lane */
  Lane *MapTaskToLane(const Task *task) override {
    // Route tasks to lanes based on their properties
    // E.g., a strongly consistent filesystem could map tasks to a lane
    // by the hash of an absolute filename path.
    return GetLaneByHash(kDefaultGroup, task->prio_, 0);
  }

  CHI_BEGIN(Destroy)
  /** Destroy labios */
  void Destroy(DestroyTask *task, RunContext &rctx) {}
  void MonitorDestroy(MonitorModeId mode, DestroyTask *task, RunContext &rctx) {
  }
  CHI_END(Destroy)

CHI_BEGIN(Read)
  /** The Read method */
  void Read(ReadTask *task, RunContext &rctx) {
    hipc::FullPtr data(task->data_);
    std::string name = task->key_.str();
    char *ret_data = data.ptr_;
    const std::string &tok_data = token_map_[name];
    memcpy(ret_data,tok_data.data(), tok_data.size());
    task->data_size_ = tok_data.size();
  }
  void MonitorRead(MonitorModeId mode, ReadTask *task, RunContext &rctx) {
    switch (mode) {
      case MonitorMode::kReplicaAgg: {
        std::vector<FullPtr<Task>> &replicas = *rctx.replicas_;
      }
    }
  }
  CHI_END(Read)

CHI_BEGIN(Write)
  /** The Write method */
  void Write(WriteTask *task, RunContext &rctx) {
    hipc::FullPtr data(task->data_);
    std::string name = task->key_.str();
    char *orig_data = data.ptr_;
    std::string tokenized_data = mytokenizer(orig_data, task->data_size_);
    token_map_[name] = std::move(tokenized_data);
  }
  void MonitorWrite(MonitorModeId mode, WriteTask *task, RunContext &rctx) {
    switch (mode) {
      case MonitorMode::kReplicaAgg: {
        std::vector<FullPtr<Task>> &replicas = *rctx.replicas_;
      }
    }
  }
  CHI_END(Write)

  CHI_AUTOGEN_METHODS  // keep at class bottom
      public:
#include "labios/labios_lib_exec.h"
};

}  // namespace chi::labios

CHI_TASK_CC(chi::labios::Server, "labios");
