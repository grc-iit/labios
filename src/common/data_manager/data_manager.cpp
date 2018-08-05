/******************************************************************************
*include files
******************************************************************************/
#include <iomanip>
#include "data_manager.h"
#include "../timer.h"

std::shared_ptr<data_manager> data_manager::instance = nullptr;
/******************************************************************************
*Interface
******************************************************************************/
std::string data_manager::get(const table &name, std::string key,std::string
server) {
#ifdef TIMERDM
    Timer t=Timer();
    t.resumeTime();
#endif
    auto return_value= aetrio_system::getInstance(service_i)->map_client->
            get(name,std::move(key),std::move(server));
#ifdef TIMERDM
    std::stringstream stream;
    stream  << "data_manager::get(),"
              <<std::fixed<<std::setprecision(10)
              <<t.pauseTime()<<"\n";
    std::cout << stream.str();
#endif
    return return_value;
}

int data_manager::put(const table &name, std::string key, std::string data,
                      std::string server) {
#ifdef TIMERDM
    Timer t=Timer();
    t.resumeTime();
#endif
    auto return_value= aetrio_system::getInstance(service_i)->map_client->
            put(name,std::move(key),data,std::move(server));
#ifdef TIMERDM
    std::stringstream stream;
    stream  << "data_manager::put(),"
              <<std::fixed<<std::setprecision(10)
              <<t.pauseTime()<<"\n";
    std::cout << stream.str();
#endif
    return return_value;
}

bool data_manager::exists(const table &name, std::string key,std::string
server) {
    return  aetrio_system::getInstance(service_i)->map_client->exists
            (name,std::move(key),std::move(server));
}

std::string data_manager::remove(const table &name, std::string key,std::string
server) {
#ifdef TIMERDM
    Timer t=Timer();
    t.resumeTime();
#endif
    auto return_value = aetrio_system::getInstance(service_i)
            ->map_client->remove(name,std::move(key),std::move(server));
#ifdef TIMERDM
    std::stringstream stream;
    stream  << "data_manager::remove(),"
              <<std::fixed<<std::setprecision(10)
              <<t.pauseTime()<<"\n";
    std::cout << stream.str();
#endif
    return return_value;
}
