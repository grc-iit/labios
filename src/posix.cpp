/******************************************************************************
*include files
******************************************************************************/
#include <cstring>
#include <functional>
#include "posix.h"
#include "common/porus_system.h"
#include "common/components/api_request.h"
#include "common/utils/Timer.h"
#include "core/metadata_manager/posix_mdm.h"
#include "core/request_handler/posix_rh.h"

FILE *porus::fopen(const char *filename, const char *mode) {
  auto api_instance =  api::getInstance();
  auto mdm_posix = std::static_pointer_cast<posix_mdm>
      (api_instance->factory_mdm->get_mdm(POSIX_MDM));
  FILE* fh;
  if(!mdm_posix->is_created(filename)){
    if(strcmp(mode,"r")==0 || strcmp(mode,"w")==0 || strcmp(mode,"a")==0){
      return nullptr;
    }else{
      fh=mdm_posix->create(filename,mode).fh;
    }
  }else{
    if(!mdm_posix->is_opened(filename))
      fh=mdm_posix->update_on_open(filename,mode).fh;
    else return nullptr;
  }
  return fh;
}

int porus::fclose(FILE *stream){
  auto api_instance =  api::getInstance();
  auto mdm_posix = std::static_pointer_cast<posix_mdm>
      (api_instance->factory_mdm->get_mdm(POSIX_MDM));
  auto file_id=mdm_posix->get_file_descriptor(stream);
  if(!mdm_posix->is_opened(file_id)) return -1;
  return mdm_posix->update_on_close(file_id).status;
}

int porus::fseek(FILE *stream, long int offset, int origin){
  auto api_instance =  api::getInstance();
  auto mdm_posix = std::static_pointer_cast<posix_mdm>
      (api_instance->factory_mdm->get_mdm(POSIX_MDM));
  auto file_id=mdm_posix->get_file_descriptor(stream);
  if( mdm_posix->get_mode(file_id)=="a" ||
      mdm_posix->get_mode(file_id)=="a+") return 0;
  auto size=mdm_posix->get_filesize(file_id);
  auto fp=mdm_posix->get_fp(file_id);
  switch(origin){
    case SEEK_SET:
      if(offset > size) return -1;
      break;
    case SEEK_CUR:
      if(fp + offset > size || fp + offset < 0) return -1;
      break;
    case SEEK_END:
      if(offset > 0) return -1;
      break;
    default:
      fprintf(stderr, "Seek origin fault!\n");
      return -1;
  }
  if(!mdm_posix->is_opened(file_id)) return -1;
  return mdm_posix->update_on_seek(file_id, offset, origin).status;
}

size_t porus::fread(void *ptr, size_t size, size_t count, FILE *stream){
  auto api_instance =  api::getInstance();
  auto mdm_posix = std::static_pointer_cast<posix_mdm>
      (api_instance->factory_mdm->get_mdm(POSIX_MDM));
  auto rh_posix = std::static_pointer_cast<posix_rh>
      (api_instance->factory_rh->get_rh(config_manager::getInstance()->POSIX_REQUEST_MODE));
  auto file_id=mdm_posix->get_file_descriptor(stream);
  if(!mdm_posix->is_opened(file_id)) return 0;
  posix_api_request request;
  request.operation=READ;
  request.size=(size * count);
  request.fd=file_id;
  posix_api_response response=rh_posix->submit(request);
  if(response.status==OPERATION_SUCCESSFUL){
    memcpy(ptr,response.data,response.size);
  }
  return response.size;
}


size_t porus::fwrite(const void *ptr, size_t size, size_t count, FILE *stream){
  auto api_instance =  api::getInstance();
  auto mdm_posix = std::static_pointer_cast<posix_mdm>
      (api_instance->factory_mdm->get_mdm(POSIX_MDM));
  auto rh_posix = std::static_pointer_cast<posix_rh>
      (api_instance->factory_rh->get_rh(config_manager::getInstance()->POSIX_REQUEST_MODE));
  auto file_id=mdm_posix->get_file_descriptor(stream);
  if(!mdm_posix->is_opened(file_id)) return 0;

  MPI_Datatype mpi_request_type=porus_system::getInstance()->build_request_dt();
  MPI_Datatype mpi_response_type=porus_system::getInstance()->build_response_dt();
  posix_api_request request;
  request.operation=WRITE;
  request.size=(size * count);
  request.fd=file_id;
  posix_api_response response=rh_posix->submit(request);
  memcached_client::getInstance()->put(response.id,(char*)ptr,DATA_SPACE);
  //Have a mode for Sync call to check the Task Queue for completion.
  return response.size;
}