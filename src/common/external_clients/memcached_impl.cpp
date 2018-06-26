/******************************************************************************
*include files
******************************************************************************/
#include "memcached_impl.h"
#include "../return_codes.h"
/******************************************************************************
*Interface
******************************************************************************/
int MemcacheDImpl::put(table table_name, std::string key, std::string value) {
    key=std::to_string(table_name)+KEY_SEPARATOR+key;
    memcached_return_t rc= memcached_set(mem_client,
                                                key.c_str(),
                                                key.length(),
                                                value.c_str(),
                                                value.length()+1,
                                                (time_t)0,
                                                (uint32_t)0);
    return rc;
}

std::string MemcacheDImpl::get(table table_name, std::string key) {
    char *return_value;
    size_t size;
    key=std::to_string(table_name)+KEY_SEPARATOR+key;
    return_value = memcached_get(mem_client,
                                 key.c_str(),
                                 key.length(),
                                 &size,
                                 (time_t)0,
                                 (uint32_t)0);
    if(return_value== nullptr){
        return "";
    }
    return return_value;
}

std::string MemcacheDImpl::remove(table table_name, std::string key) {
    key=std::to_string(table_name)+KEY_SEPARATOR+key;
    size_t size;
    std::string value=memcached_get(mem_client,
                                    key.c_str(),
                                    key.length(),
                                    &size ,
                                    (time_t)0,
                                    (uint32_t)0);;
    memcached_delete(mem_client, key.c_str(), key.length(), (time_t)0);
    return value;
}

bool MemcacheDImpl::exists(table table_name, std::string key) {
    key=std::to_string(table_name)+KEY_SEPARATOR+key;
    memcached_return_t rc= memcached_exist(mem_client,key.c_str(),key.size());
    return rc == memcached_return_t::MEMCACHED_SUCCESS;
}

