/******************************************************************************
*include files
******************************************************************************/
#include "memcached_impl.h"

/******************************************************************************
*Interface
******************************************************************************/
int MemcacheDImpl::put(const table &name, std::string key, const std::string &value) {
    key=std::to_string(name)+KEY_SEPARATOR+key;
    memcached_return_t rc= memcached_set(mem_client,
                                                key.c_str(),
                                                key.length(),
                                                value.c_str(),
                                                value.length()+1,
                                                (time_t)0,
                                                (uint32_t)0);
    return rc;
}

std::string MemcacheDImpl::get(const table &name, std::string key) {
    char *return_value;
    size_t size;
    key=std::to_string(name)+KEY_SEPARATOR+key;
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

std::string MemcacheDImpl::remove(const table &name, std::string key) {
    key=std::to_string(name)+KEY_SEPARATOR+key;
    memcached_delete(mem_client, key.c_str(), key.length(), (time_t)0);
    return "";
}

bool MemcacheDImpl::exists(const table &name, std::string key) {
    key=std::to_string(name)+KEY_SEPARATOR+key;
    memcached_return_t rc= memcached_exist(mem_client,key.c_str(),key.size());
    return rc == memcached_return_t::MEMCACHED_SUCCESS;
}

bool MemcacheDImpl::purge() {
    memcached_flush(mem_client,0);
}

