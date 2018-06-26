/******************************************************************************
*include files
******************************************************************************/
#include <cmath>
#include "task_builder.h"
#include "../data_structures.h"
#include "../metadata_manager/metadata_manager.h"
#include <memory>
std::shared_ptr<task_builder> task_builder::instance = nullptr;
/******************************************************************************
*Interface
******************************************************************************/
std::vector<write_task> task_builder::build_write_task(write_task task,
                                                       std::string data) {
    auto tasks = std::vector<write_task>();
    file source = task.source;
    int number_of_tasks= static_cast<int>(ceil((float)(source.offset + source.size) / MAX_IO_UNIT));
    auto map_client=aetrio_system::getInstance(service_i)->map_client;
    auto map_server=aetrio_system::getInstance(service_i)->map_server;
    int dataspace_id = static_cast<int>(std::chrono::duration_cast<std::chrono::microseconds>
            (std::chrono::system_clock::now().time_since_epoch()).count());
    std::size_t chunk_index=(source.offset/ MAX_IO_UNIT);
    std::size_t base_offset = chunk_index*MAX_IO_UNIT+source.offset%MAX_IO_UNIT;
    std::size_t data_offset = 0;
    std::size_t remaining_data = source.size;

    for(int i=0;i<number_of_tasks;i++){
        auto sub_task = new write_task(task);
        sub_task->task_id= static_cast<int64_t>(std::chrono::duration_cast<std::chrono::microseconds>
                (std::chrono::system_clock::now().time_since_epoch()).count());
        /*
         * write not aligned to 2 MB offsets
         */
        if(base_offset!=i*MAX_IO_UNIT){

            std::string chunk_str=map_client->get(table::CHUNK_DB, source.filename +std::to_string(base_offset));
            chunk_meta chunk_meta= serialization_manager().deserialize_chunk(chunk_str);
            /*
             * chunk in dataspace
             */
            if(chunk_meta.destination.dest_t==source_type::DATASPACE_LOC){
                /*
                 * update new data in dataspace
                 */
                auto chunk_value=map_client->get(table::DATASPACE_DB,chunk_meta.destination.filename);
                chunk_value.replace(base_offset,MAX_IO_UNIT-base_offset,data.substr(data_offset,MAX_IO_UNIT-base_offset));
                map_client->put(table::DATASPACE_DB,std::to_string(dataspace_id)+"_"+std::to_string(i),chunk_value);
                /*
                 * build new  task
                 */
                sub_task->destination.size=chunk_meta.destination.size;
                sub_task->destination.offset=0;
                sub_task->source.offset=source.offset;
                sub_task->source.size=sub_task->destination.size;
                sub_task->source.filename=source.filename;
                sub_task->destination.dest_t=source_type::DATASPACE_LOC;
                sub_task->destination.filename=std::to_string(dataspace_id)+"_"+std::to_string(i);;
            }else{
                /*
                 * chunk in file
                 */
                sub_task->destination.worker=chunk_meta.destination.worker;
                sub_task->destination.size=MAX_IO_UNIT-base_offset;
                sub_task->destination.offset=0;
                sub_task->source.offset=source.offset;
                sub_task->source.size=sub_task->destination.size;
                sub_task->source.filename=source.filename;
                sub_task->destination.dest_t=source_type::DATASPACE_LOC;
                sub_task->destination.filename=std::to_string(dataspace_id)+"_"+std::to_string(i);
                sub_task->meta_updated=true;
            }
            base_offset+=(MAX_IO_UNIT-base_offset);
            remaining_data-=(MAX_IO_UNIT-base_offset);
        }else{
            /*
             * chunk aligns 2MB offsets
             */
            /*
             * remaining_data I/O is less than 2 MB
             */
            if(remaining_data<MAX_IO_UNIT){
                if(!map_client->exists(table::CHUNK_DB, source.filename +std::to_string(base_offset))){
                    sub_task->destination.size=remaining_data;
                    sub_task->destination.offset=0;
                    sub_task->source.offset=base_offset;
                    sub_task->source.size=sub_task->destination.size;
                    sub_task->source.filename=source.filename;
                    sub_task->destination.dest_t=source_type::DATASPACE_LOC;
                    sub_task->destination.filename=std::to_string(dataspace_id)+"_"+std::to_string(i);
                }else{
                    std::string chunk_str=map_client->get(table::CHUNK_DB, source.filename +std::to_string(base_offset));
                    chunk_meta chunk_meta= serialization_manager().deserialize_chunk(chunk_str);
                    /*
                     * chunk in dataspace
                     */
                    if(chunk_meta.destination.dest_t==source_type::DATASPACE_LOC){
                        /*
                         * update new data in dataspace
                         */
                        auto chunk_value=map_client->get(table::DATASPACE_DB,chunk_meta.destination.filename);
                        chunk_value.replace(base_offset,MAX_IO_UNIT-base_offset,data.substr(data_offset,MAX_IO_UNIT-base_offset));
                        map_client->put(table::DATASPACE_DB,std::to_string(dataspace_id)+"_"+std::to_string(i),chunk_value);
                        /*
                         * build new  task
                         */
                        sub_task->destination.size=chunk_meta.destination.size;
                        sub_task->destination.offset=0;
                        sub_task->source.offset=source.offset;
                        sub_task->source.size=sub_task->destination.size;
                        sub_task->source.filename=source.filename;
                        sub_task->destination.dest_t=source_type::DATASPACE_LOC;
                        sub_task->destination.filename=std::to_string(dataspace_id)+"_"+std::to_string(i);;
                    }else{
                        /*
                         * chunk in file
                         */
                        sub_task->destination.worker=chunk_meta.destination.worker;
                        sub_task->destination.size=MAX_IO_UNIT-base_offset;
                        sub_task->destination.offset=0;
                        sub_task->source.offset=source.offset;
                        sub_task->source.size=sub_task->destination.size;
                        sub_task->source.filename=source.filename;
                        sub_task->destination.dest_t=source_type::DATASPACE_LOC;
                        sub_task->destination.filename=std::to_string(dataspace_id)+"_"+std::to_string(i);
                        sub_task->meta_updated=true;
                    }
                }

                base_offset+=remaining_data;
                remaining_data-=remaining_data;
            }else{
                /*
                 * remaining_data >= 2MB
                 */
                sub_task->destination.size=MAX_IO_UNIT;
                sub_task->destination.offset=0;
                sub_task->source.offset=base_offset;
                sub_task->source.size=sub_task->destination.size;
                sub_task->source.filename=source.filename;
                sub_task->destination.dest_t=source_type::DATASPACE_LOC;
                sub_task->destination.filename=std::to_string(dataspace_id)+"_"+std::to_string(i);
                base_offset+=MAX_IO_UNIT;
                remaining_data-=MAX_IO_UNIT;
            }

        }
        tasks.push_back(*sub_task);
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
        rt->task_id = static_cast<int64_t>(std::chrono::duration_cast<std::chrono::microseconds>
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




