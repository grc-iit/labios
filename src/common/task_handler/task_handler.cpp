//
// Created by hariharan on 2/23/18.
//

#include <cmath>
#include "task_handler.h"
#include "../data_structures.h"
#include "../metadata_manager/metadata_manager.h"

std::shared_ptr<task_handler> task_handler::instance = nullptr;

int task_handler::submit(task *task_t){
    return dq->publish_task(task_t);
}

std::vector<write_task> task_handler::build_task_write(write_task task,std::string data) {
    std::vector<write_task> tasks=std::vector<write_task>();
    serialization_manager sm=serialization_manager();
    file source=task.source;
    int number_of_tasks= static_cast<int>(ceil((float)(source.offset + source.size) / io_unit_max));
    auto map=aetrio_system::getInstance(service_i)->map_client;
    auto value=map->get(table::DATASPACE_DB,"count");
    int dataspace_id=0;
    if(strcmp(value.c_str(),"")!=0){
        dataspace_id=atoi(value.c_str());
        dataspace_id++;
    }

    size_t chunk_index=(source.offset/ io_unit_max);
    size_t base_offset=chunk_index*io_unit_max+source.offset%io_unit_max;
    size_t data_offset=0;
    size_t left=source.size;
    for(int i=0;i<number_of_tasks;i++){
        write_task sub_task=write_task(task);
        if(base_offset!=i*io_unit_max){
            std::string chunk_str=map->get(table::CHUNK_DB, source.filename +std::to_string(base_offset));
            chunk_meta chunk_meta=sm.deserialise_chunk(chunk_str);
            if(chunk_meta.destination.dest_t==source_type::DATASPACE_LOC){
                auto chunk_value=map->get(table::DATASPACE_DB,chunk_meta.destination.filename);
                chunk_value.replace(base_offset,io_unit_max-base_offset,data.substr(data_offset,io_unit_max-base_offset));
                map->put(table::DATASPACE_DB,chunk_meta.destination.filename,chunk_value);

                sub_task.destination.size=io_unit_max-base_offset;
                sub_task.destination.offset=0;
                sub_task.source.offset=source.offset+sub_task.destination.offset;
                sub_task.source.size=sub_task.destination.size;

                sub_task.destination.dest_t=source_type::DATASPACE_LOC;
                sub_task.destination.filename=chunk_meta.destination.filename;
                sub_task.meta_updated=true;
            }else{
                sub_task.destination.size=io_unit_max-base_offset;
                sub_task.destination.offset=0;
                sub_task.source.offset=source.offset+sub_task.destination.offset;
                sub_task.source.size=sub_task.destination.size;
                sub_task.destination.dest_t=source_type::DATASPACE_LOC;
                sub_task.destination.filename=std::to_string(dataspace_id)+"_"+std::to_string(i);
            }
            base_offset+=(io_unit_max-base_offset);
            left-=(io_unit_max-base_offset);
        }else{
            if(left<io_unit_max){
                sub_task.destination.size=left;
                sub_task.destination.offset=0;
                sub_task.source.offset=base_offset;
                sub_task.source.size=sub_task.destination.size;
                sub_task.destination.dest_t=source_type::DATASPACE_LOC;
                sub_task.destination.filename=std::to_string(dataspace_id)+"_"+std::to_string(i);
                base_offset+=left;
                left-=left;
            }else{
                sub_task.destination.size=io_unit_max;
                sub_task.destination.offset=0;
                sub_task.source.offset=base_offset;
                sub_task.source.size=sub_task.destination.size;
                sub_task.destination.dest_t=source_type::DATASPACE_LOC;
                sub_task.destination.filename=std::to_string(dataspace_id)+"_"+std::to_string(i);
                base_offset+=io_unit_max;
                left-=io_unit_max;
            }

        }
        tasks.push_back(sub_task);
    }
    return tasks;
}

std::vector<read_task> task_handler::build_task_read(read_task task) {
    std::vector<read_task> tasks=std::vector<read_task>();
    std::shared_ptr<metadata_manager> mdm=metadata_manager::getInstance(LIB);
    auto chunks=mdm->fetch_chunks(task);
    for(auto chunk:chunks){
        read_task rt;
        rt.source=chunk.destination;
        tasks.push_back(rt);
    }
    return tasks;
}




