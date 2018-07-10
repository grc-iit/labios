/******************************************************************************
*include files
******************************************************************************/
#include <cmath>
#include "task_builder.h"
#include "../metadata_manager/metadata_manager.h"

std::shared_ptr<task_builder> task_builder::instance = nullptr;
/******************************************************************************
*Interface
******************************************************************************/
std::vector<write_task*> task_builder::build_write_task(write_task task,
                                                       std::string data) {
    auto map_client=aetrio_system::getInstance(service_i)->map_client;
    auto map_server=aetrio_system::getInstance(service_i)->map_server;
    auto tasks = std::vector<write_task*>();
    file source = task.source;

    auto number_of_tasks = static_cast<int>(std::ceil((float)(source.size) / MAX_IO_UNIT));
    auto dataspace_id = static_cast<int64_t>
            (std::chrono::duration_cast<std::chrono::microseconds>
            (std::chrono::system_clock::now().time_since_epoch()).count());

    std::size_t base_offset = (source.offset / MAX_IO_UNIT) * MAX_IO_UNIT +
            source.offset % MAX_IO_UNIT;
    std::size_t data_offset = 0;
    std::size_t remaining_data = source.size;

    for(int i=0;i<number_of_tasks;i++){
        std::size_t chunk_index = base_offset / MAX_IO_UNIT;
        auto sub_task = new write_task(task);
        sub_task->task_id = static_cast<int64_t>
                (std::chrono::duration_cast<std::chrono::microseconds>
                (std::chrono::system_clock::now().time_since_epoch()).count());
/****************** write not aligned to 2 MB offsets ************************/
        if(base_offset != chunk_index * MAX_IO_UNIT){
            size_t bucket_offset=base_offset-chunk_index * MAX_IO_UNIT;
            std::string chunk_str = map_client->get(
                    table::CHUNK_DB,
                    source.filename + std::to_string(chunk_index * MAX_IO_UNIT));
            chunk_meta cm =
                    serialization_manager().deserialize_chunk(chunk_str);
/*************************** chunk in dataspace ******************************/
            if(cm.destination.location == location_type::CACHE){
                /********* update new data in dataspace **********/
                auto chunk_value=map_client->get(
                        table::DATASPACE_DB,
                        cm.destination.filename);
                std::size_t size_to_write=0;
                if(remaining_data < MAX_IO_UNIT){
                    size_to_write=remaining_data;
                }else{
                    size_to_write=MAX_IO_UNIT;
                }
                if(chunk_value.length() >= bucket_offset+size_to_write){
                    chunk_value.replace(bucket_offset,
                                        size_to_write,
                                        data.substr(data_offset,
                                                    bucket_offset));
                }else{
                    chunk_value=chunk_value.substr(0,bucket_offset)+data;
                }
                map_client->put(
                        table::DATASPACE_DB,
                        cm.destination.filename,
                        chunk_value);
                sub_task->addDataspace=false;
                if(chunk_value.length()==MAX_IO_UNIT) {
                    sub_task->publish=true;
                    sub_task->destination.size=MAX_IO_UNIT;
                    sub_task->destination.offset=0;
                    sub_task->source.offset=0;
                    sub_task->source.size=MAX_IO_UNIT;
                    sub_task->source.filename=source.filename;
                    sub_task->destination.location=location_type::CACHE;
                    sub_task->destination.filename =
                            std::to_string(dataspace_id) + "_" +
                            std::to_string(i);
                }else{
                    /********* build new task **********/
                    sub_task->destination.size=size_to_write;
                    sub_task->destination.offset=bucket_offset;
                    sub_task->source.offset=bucket_offset;
                    sub_task->source.size=size_to_write;
                    sub_task->source.filename=source.filename;
                    sub_task->destination.location=location_type::CACHE;
                    sub_task->destination.filename =
                            std::to_string(dataspace_id) + "_" +
                            std::to_string(chunk_index);
                }


            }else{
/****************************** chunk in file ********************************/
                std::size_t size_to_write=0;
                if(remaining_data < MAX_IO_UNIT){
                    size_to_write=remaining_data;
                }else{
                    size_to_write=MAX_IO_UNIT;
                    sub_task->publish=true;
                }
                sub_task->destination.worker=cm.destination.worker;
                sub_task->destination.size=size_to_write;
                sub_task->destination.offset=bucket_offset;
                sub_task->source.offset=bucket_offset;
                sub_task->source.size=size_to_write;
                sub_task->source.filename=source.filename;
                sub_task->destination.location=location_type::CACHE;
                sub_task->destination.filename =
                        std::to_string(dataspace_id) + "_" +
                        std::to_string(chunk_index);
                sub_task->meta_updated=true;
            }
            std::size_t size_to_write=0;
            if(remaining_data < MAX_IO_UNIT){
                size_to_write=remaining_data;
            }else{
                size_to_write=MAX_IO_UNIT;
            }
            base_offset+=size_to_write;
            data_offset+=size_to_write;
            remaining_data-=size_to_write;
        }
/******************** write aligned to 2 MB offsets **************************/
        else{
            size_t bucket_offset=0;
            /******* remaining_data I/O is less than 2 MB *******/
            if(remaining_data < MAX_IO_UNIT){
                if(!map_client->exists(table::CHUNK_DB,
                                       source.filename +
                                       std::to_string(base_offset))){
                    sub_task->publish=false;
                    sub_task->destination.size = remaining_data;
                    sub_task->destination.offset = bucket_offset;
                    sub_task->source.offset = bucket_offset;
                    sub_task->source.size = sub_task->destination.size;
                    sub_task->source.filename = source.filename;
                    sub_task->destination.location = location_type::CACHE;
                    sub_task->destination.filename =
                            std::to_string(dataspace_id) + "_" +
                            std::to_string(chunk_index);
                }else{
                    std::string chunk_str = map_client->get(
                            table::CHUNK_DB,
                            source.filename + std::to_string(base_offset));
                    chunk_meta cm =
                            serialization_manager().deserialize_chunk(chunk_str);
                    /********* chunk in dataspace **********/
                    if(cm.destination.location == location_type::CACHE){
                        //update new data in dataspace
                        auto chunk_value = map_client->get(
                                table::DATASPACE_DB,
                                cm.destination.filename);
                        if(chunk_value.size() >= bucket_offset +remaining_data){
                            chunk_value.replace(bucket_offset,
                                                remaining_data,
                                                data.substr
                                                        (data_offset,
                                                         remaining_data));
                        }else{
                            chunk_value=chunk_value.substr(0,bucket_offset-1)+data;
                        }
                        map_client->put(
                                table::DATASPACE_DB,
                                cm.destination.filename,
                                chunk_value);
                        //build new task
                        sub_task->addDataspace=false;
                        sub_task->publish=false;
                        sub_task->destination.size=remaining_data;//cm
                                // .destination
                                // .size;
                        sub_task->destination.offset=bucket_offset;
                        sub_task->source.offset=bucket_offset;
                        sub_task->source.size=remaining_data;
                        sub_task->source.filename=source.filename;
                        sub_task->destination.location=location_type::CACHE;
                        sub_task->destination.filename =
                                std::to_string(dataspace_id) + "_" +
                                std::to_string(chunk_index);
                    }
                    /************ chunk in file *************/
                    else{
                        sub_task->publish=false;
                        sub_task->destination.worker=cm.destination.worker;
                        sub_task->destination.size=remaining_data;
                        sub_task->destination.offset=bucket_offset;
                        sub_task->source.offset=bucket_offset;
                        sub_task->source.size=remaining_data;
                        sub_task->source.filename=source.filename;
                        sub_task->destination.location=location_type::CACHE;
                        sub_task->destination.filename =
                                std::to_string(dataspace_id) + "_" +
                                std::to_string(chunk_index);
                        sub_task->meta_updated=true;
                    }
                }

                base_offset+=remaining_data;
                data_offset+=remaining_data;
                remaining_data=0;
            }
            /************** remaining_data is >= 2 MB ***********/
            else{
                sub_task->publish=true;
                sub_task->destination.size = MAX_IO_UNIT;
                sub_task->destination.offset = bucket_offset;
                sub_task->source.offset = bucket_offset;
                sub_task->source.size = MAX_IO_UNIT;
                sub_task->source.filename = source.filename;
                sub_task->destination.location = location_type::CACHE;
                sub_task->destination.filename =
                        std::to_string(dataspace_id) + "_" +
                        std::to_string(chunk_index);
                base_offset += MAX_IO_UNIT;
                data_offset += MAX_IO_UNIT;
                remaining_data -= MAX_IO_UNIT;
            }

        }
        tasks.emplace_back(sub_task);
    }
    return tasks;
}

std::vector<read_task> task_builder::build_read_task(read_task task) {
    auto tasks = std::vector<read_task>();
    auto mdm = metadata_manager::getInstance(LIB);
    auto map_server = aetrio_system::getInstance(service_i)->map_server;
    auto chunks = mdm->fetch_chunks(task);

    for(auto chunk:chunks){
        auto rt = new read_task();

        rt->task_id = static_cast<int64_t>
                (std::chrono::duration_cast<std::chrono::microseconds>
                (std::chrono::system_clock::now().time_since_epoch()).count());
        rt->source = chunk.destination;
        rt->destination.filename = std::to_string(static_cast<uint64_t>
                (std::chrono::duration_cast<std::chrono::microseconds>
                        (std::chrono::system_clock::now().time_since_epoch()).count()));
        tasks.push_back(*rt);
        delete(rt);
    }
    return tasks;
}





