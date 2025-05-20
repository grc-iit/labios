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
  Client client;

  class Server : public Module {
  public:
    CLS_CONST LaneGroupId kDefaultGroup = 0;
    std::unordered_map<std::string, std::string> token_map_;
    std::unordered_map<hipc::string, LabiosMd> md_;
    
  public:
    Server() = default;

    template<typename TaskT>
    void DataRoute(TaskT *task) {
      client.Init(id_);
      LabiosMd md = client.MdGetOrCreate(HSHM_MCTX , DomainQuery::GetDynamic(), task->key_.str(), task->offset_, task->data_size_, DomainQuery::GetDirectId(SubDomain::kGlobalContainers,container_id_,0));
      task->dom_query_ = md.loc_;
      task->SetDirect();
      task->UnsetRouted();
    }
    template <typename TaskT>
    void MdRoute(TaskT *task) {
      // Concretize the domain to map the task
      task->dom_query_ = chi::DomainQuery::GetDirectHash(
        chi::SubDomainId::kGlobalContainers, 
        std::hash<std::string>{}(task->key_.str()) + std::hash<size_t>{}(task->off_));
      task->SetDirect();
      task->UnsetRouted();
    }

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
        case MonitorMode::kSchedule: {
          DataRoute<ReadTask>(task);
          return;
        }
        // case MonitorMode::kReplicaAgg: {
        //   std::vector<FullPtr<Task>> &replicas = *rctx.replicas_;
        // }
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
        case MonitorMode::kSchedule: {
          DataRoute<WriteTask>(task);
          return;
        }
        // case MonitorMode::kReplicaAgg: {
        //   std::vector<FullPtr<Task>> &replicas = *rctx.replicas_;
        // }
      }
    }
    CHI_END(Write)

  CHI_BEGIN(MdGetOrCreate)
    /** The MdGetOrCreate method */
    void MdGetOrCreate(MdGetOrCreateTask *task, RunContext &rctx) {
      std::string key = task->key_.str();
      LabiosMd &md = md_[key];
      md.key_ = key;
      md.offset_ = task->off_;
      md.size_ = task->size_;
      md.loc_ = task->loc_;
      // md_[task->key_] = md;
    }
    void MonitorMdGetOrCreate(MonitorModeId mode, MdGetOrCreateTask *task, RunContext &rctx) {
      switch (mode) {
        case MonitorMode::kSchedule: {
          MdRoute<MdGetOrCreateTask>(task);
          return;
        }
        // case MonitorMode::kReplicaAgg: {
        //   std::vector<FullPtr<Task>> &replicas = *rctx.replicas_;
        // }
      }
    }
    CHI_END(MdGetOrCreate)

    CHI_AUTOGEN_METHODS  // keep at class bottom
        public:
  #include "labios/labios_lib_exec.h"
  };

}  // namespace chi::labios

CHI_TASK_CC(chi::labios::Server, "labios");
