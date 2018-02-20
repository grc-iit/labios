//
// Created by anthony on 5/17/17.
//

#ifndef PORUS_ENUMERATORS_H
#define PORUS_ENUMERATORS_H

typedef enum posix_operation{
  OPEN = 0,
  CLOSE =1,
  READ = 2,
  WRITE = 3
} ;

typedef enum request_status{
  COMPLETED = 0,
  PENDING =1
};

typedef enum api_type{
  POSIX = 0
};

typedef enum worker_status{
  ACTIVATED = 0,
  SUSPENDED = 1
};

typedef enum worker_load{
  NO_LOAD = 4,
  LIGHT_LOAD = 3,
  MEDIUM_LOAD = 2,
  HIGH_LOAD = 1,
  FULL_LOAD = 0
};

#endif //PORUS_ENUMERATORS_H