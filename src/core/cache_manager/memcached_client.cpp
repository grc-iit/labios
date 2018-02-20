//
// Created by anthony on 5/18/17.
//

#include <cstring>
#include "memcached_client.h"
#include "../../../include/config_manager.h"
#include "../../common/porus_system.h"
#include <libmemcached/memcached.hpp>
/******************************************************************************
*Initialization of static members
******************************************************************************/

std::shared_ptr<memcached_client> memcached_client::instance = nullptr;

