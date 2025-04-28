#ifndef CHI_LABIOS_METHODS_H_
#define CHI_LABIOS_METHODS_H_

/** The set of methods in the admin task */
struct Method : public TaskMethod {
  TASK_METHOD_T kRead = 10;
  TASK_METHOD_T kWrite = 11;
  TASK_METHOD_T kCount = 12;
};

#endif  // CHI_LABIOS_METHODS_H_