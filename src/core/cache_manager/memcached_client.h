//
// Created by anthony on 5/18/17.
//

#ifndef PORUS_MEMCACHED_CLIENT_H
#define PORUS_MEMCACHED_CLIENT_H

#include <memory>
#include <libmemcached/memcached.h>
#include "../../common/components/api_response.h"
#include "../../common/porus_system.h"



class memcached_client{
private:
/******************************************************************************
*Variables and members
******************************************************************************/
  static std::shared_ptr<memcached_client> instance;
  memcached_st * mem_client;
/******************************************************************************
*Constructors
******************************************************************************/
  memcached_client(){
    init();
  }
  memcached_client(std::string config){
    init(config);
  }
/******************************************************************************
*Functions
******************************************************************************/
  inline void init() {
    auto config_string=config_manager::getInstance()->config_string;
    mem_client = memcached(config_string.c_str(), config_string.length());
  }
  inline void init(std::string config_string) {
    mem_client = memcached(config_string.c_str(), config_string.length());
  }
public:
/******************************************************************************
*Destructor
******************************************************************************/
  virtual ~memcached_client(){}
/******************************************************************************
*Getters and setters
******************************************************************************/
  inline static std::shared_ptr<memcached_client> getInstance(){
    return std::shared_ptr<memcached_client>(new memcached_client());
  }
  inline static std::shared_ptr<memcached_client> getInstance(std::string
                                                              config){
    return std::shared_ptr<memcached_client>(new memcached_client(config));
  }
  inline static std::string build_config_string(std::string ip){
    return "--SERVER="+ip;
  }
/******************************************************************************
*Interface
******************************************************************************/
  inline bool key_exists(std::string key, std::string key_space) {
    key=key_space+key;
    auto status=memcached_exist(mem_client, key.c_str(), key.length());
    return status==MEMCACHED_SUCCESS;
  }
  template <typename RESPONSE=api_response>
  inline RESPONSE put(std::string key, std::string value,
                                 std::string key_space) {
    auto memcached_server=porus_system::getInstance()->get_memcached_server();
    key=key_space+key;
    memcached_return_t rc= memcached_set_by_key(mem_client,
                                                memcached_server.c_str(),
                                                memcached_server.length(),
                                                key.c_str(),
                                                key.length(),
                                                value.c_str(),
                                                value.length(),
                                                (time_t)0,
                                                (uint32_t)0);
    return posix_api_response();
  }
  template <typename RESPONSE=api_response>
  inline RESPONSE get(std::string key, std::string key_space) {
    char *return_value;
    size_t size;
    key=key_space+key;
    return_value = memcached_get(mem_client,
                                 key.c_str(),
                                 key.length(),
                                 &size ,
                                 (time_t)0,
                                 (uint32_t)0);
    auto response=posix_api_response();
    response.data=return_value;
    response.size=size;
    return response;
  }
  template <typename RESPONSE=api_response>
  inline RESPONSE replace(std::string key, std::string value,
                                     std::string key_space) {
    auto memcached_server=porus_system::getInstance()->get_memcached_server();
    key=key_space+key;
    memcached_return_t rc= memcached_replace_by_key(mem_client,
                                                    memcached_server.c_str(),
                                                    memcached_server.length(),
                                                    key.c_str(),
                                                    key.length(),
                                                    value.c_str(),
                                                    value.length(),
                                                    (time_t)0,
                                                    (uint32_t)0);
    return posix_api_response();
  }
  template <typename RESPONSE=api_response>
  inline RESPONSE remove(std::string key, std::string key_space) {
    key=key_space+key;
    memcached_delete(mem_client, key.c_str(), key.length(), (time_t)0);
    return posix_api_response();
  }
  template <typename RESPONSE=api_response>
  inline RESPONSE append(std::string key, std::string value,
                          std::string key_space) {
    auto memcached_server=porus_system::getInstance()->get_memcached_server();
    key=key_space+key;
    memcached_return_t rc= memcached_append_by_key(mem_client,
                                                    memcached_server.c_str(),
                                                    memcached_server.length(),
                                                    key.c_str(),
                                                    key.length(),
                                                    value.c_str(),
                                                    value.length(),
                                                    (time_t)0,
                                                    (uint32_t)0);
    return posix_api_response();
  }
};


#endif //PORUS_MEMCACHED_CLIENT_H
