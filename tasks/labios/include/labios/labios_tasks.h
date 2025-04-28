//
// Created by lukemartinlogan on 8/11/23.
//

#ifndef CHI_TASKS_TASK_TEMPL_INCLUDE_labios_labios_TASKS_H_
#define CHI_TASKS_TASK_TEMPL_INCLUDE_labios_labios_TASKS_H_

#include "chimaera/chimaera_namespace.h"

namespace chi::labios {

#include "labios_methods.h"
CHI_NAMESPACE_INIT

CHI_BEGIN(Create)
/** A task to create labios */
struct CreateTaskParams {
  CLS_CONST char *lib_name_ = "labios_labios";
  int labios_id_;

  HSHM_INLINE_CROSS_FUN
  CreateTaskParams() = default;

  HSHM_INLINE_CROSS_FUN
  CreateTaskParams(const hipc::CtxAllocator<CHI_ALLOC_T> &alloc, int labios_id=0) {labios_id_=labios_id;}

  template <typename Ar>
  HSHM_INLINE_CROSS_FUN void serialize(Ar &ar) {
    labios_id_;
  }
};
typedef chi::Admin::CreatePoolBaseTask<CreateTaskParams> CreateTask;
CHI_END(Create)

CHI_BEGIN(Destroy)
/** A task to destroy labios */
typedef chi::Admin::DestroyContainerTask DestroyTask;
CHI_END(Destroy)

CHI_BEGIN(Read)
/** The ReadTask task */
struct ReadTask : public Task, TaskFlags<TF_SRL_SYM> {

  IN hipc::string key_;
  OUT hipc::Pointer data_;
  OUT size_t data_size_;
  /** SHM default constructor */
  HSHM_INLINE explicit
  ReadTask(const hipc::CtxAllocator<CHI_ALLOC_T> &alloc) : Task(alloc) {}

  /** Emplace constructor */
  HSHM_INLINE explicit
  ReadTask(const hipc::CtxAllocator<CHI_ALLOC_T> &alloc,
                const TaskNode &task_node,
                const PoolId &pool_id,
                const DomainQuery &dom_query,
                const std::string &key, hipc::Pointer &data, size_t data_size) : Task(alloc) {
    // Initialize task
    task_node_ = task_node;
    prio_ = TaskPrioOpt::kLowLatency;
    pool_ = pool_id;
    method_ = Method::kRead;
    task_flags_.SetBits(0);
    dom_query_ = dom_query;

    // Custom
    key_= key;
    data_ = data;
    data_size_ = data_size;
  }

  /** Duplicate message */
  void CopyStart(const ReadTask &other, bool deep) {
    key_= other.key_;
    data_ = other.data_;
    data_size_ = other.data_size_;
    if (!deep) {
      UnsetDataOwner();
    }
  }

  /** (De)serialize message call */
  template<typename Ar>
  void SerializeStart(Ar &ar) {
    ar(key_);
    ar.bulk(DT_WRITE, data_, data_size_);
  }

  /** (De)serialize message return */
  template<typename Ar>
  void SerializeEnd(Ar &ar) {
    ar.bulk(DT_WRITE, data_, data_size_);
  }
};
CHI_END(Read);

CHI_BEGIN(Write)
/** The WriteTask task */
struct WriteTask : public Task, TaskFlags<TF_SRL_SYM> {
  IN hipc::string key_;
  IN hipc::Pointer data_;
  IN size_t data_size_;
  /** SHM default constructor */
  HSHM_INLINE explicit
  WriteTask(const hipc::CtxAllocator<CHI_ALLOC_T> &alloc) : Task(alloc) {}

  /** Emplace constructor */
  HSHM_INLINE explicit
  WriteTask(const hipc::CtxAllocator<CHI_ALLOC_T> &alloc,
                const TaskNode &task_node,
                const PoolId &pool_id,
                const DomainQuery &dom_query,
                const std::string &key, hipc::Pointer &data, size_t data_size) : Task(alloc) {
    // Initialize task
    task_node_ = task_node;
    prio_ = TaskPrioOpt::kLowLatency;
    pool_ = pool_id;
    method_ = Method::kWrite;
    task_flags_.SetBits(0);
    dom_query_ = dom_query;

    // Custom
    key_= key;
    data_ = data;
    data_size_ = data_size;
  }

  /** Duplicate message */
  void CopyStart(const WriteTask &other, bool deep) {
    key_= other.key_;
    data_ = other.data_;
    data_size_ = other.data_size_;
    if (!deep) {
      UnsetDataOwner();
    }
  }

  /** (De)serialize message call */
  template<typename Ar>
  void SerializeStart(Ar &ar) {
    ar(key_);
    ar.bulk(DT_EXPOSE, data_, data_size_);
  }

  /** (De)serialize message return */
  template<typename Ar>
  void SerializeEnd(Ar &ar) {
    ar(key_);
    ar.bulk(DT_WRITE, data_, data_size_);
  }
};
CHI_END(Write);

CHI_AUTOGEN_METHODS  // keep at class bottom

}  // namespace chi::labios

#endif  // CHI_TASKS_TASK_TEMPL_INCLUDE_labios_labios_TASKS_H_
