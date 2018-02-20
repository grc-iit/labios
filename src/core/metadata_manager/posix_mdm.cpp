//
// Created by anthony on 5/11/17.
//

#include <cstring>
#include "posix_mdm.h"
#include "../../common/utils/google_tools/city.h"
#include "../../common/constants.h"
#include "../../common/return_codes.h"

/******************************************************************************
*Initialization of static members
******************************************************************************/
std::shared_ptr<posix_mdm> posix_mdm::instance=nullptr;
/******************************************************************************
*Functions
******************************************************************************/
uint32_t posix_mdm::hash_filename(std::string filename) {
  return CityHash32(filename.c_str(), filename.length());
}

uint32_t posix_mdm::get_filename(int fildes) {
  return (uint32_t) atoi((char*)mem_client->
      get(std::to_string(fildes),FH_TO_FILE).data);
}

size_t posix_mdm::get_filesize(std::string filename) {
  return (size_t) atoi((char*)mem_client->get(filename, FILE_TO_SIZE).data);
}

size_t posix_mdm::get_filesize(int fildes) {
  return get_filesize(std::to_string(get_filename(fildes)));
}

long posix_mdm::get_fp(int fildes) {
  return atol((char*)mem_client->get(std::to_string(fildes), FH_TO_FP).data);
}
/******************************************************************************
*Interface
******************************************************************************/
int posix_mdm::get_file_descriptor(FILE * fh) {
  return fileno(fh);
}

bool posix_mdm::is_created(std::string filename) {
  auto file_hash = hash_filename(filename);
  void* size = mem_client->get(std::to_string(file_hash), FILENAME).data;
  return size != nullptr;
}

bool posix_mdm::is_opened(std::string filename) {
  void* file_desc= mem_client->get(filename,FILENAME).data;
  return file_desc!= nullptr;
}

bool posix_mdm::is_opened(int fildes) {
  return mem_client->get(std::to_string(fildes), FH_TO_FILE).data != nullptr;
}

bool posix_mdm::check_mode(int fildes, std::string mode) {
  return strcmp((char*)mem_client->get(std::to_string(fildes), FH_TO_MODE)
      .data, mode.c_str())==0;
}

std::string posix_mdm::get_mode(int fildes) {
  return (char*)mem_client->get(std::to_string(fildes), FH_TO_MODE).data;
}

posix_api_response posix_mdm::create(std::string filename, std::string mode) {
  if(filename.length() > MAX_FILENAME_LENGTH)
    return posix_api_response(METADATA_CREATION_FAILED);
  auto file_hash = hash_filename(filename);
  mem_client->put(std::to_string(file_hash), std::to_string(0), FILE_TO_SIZE);
  FILE* fh = fmemopen(NULL, 1, mode.c_str());
  int fd = fileno(fh);
  mem_client->put(std::to_string(file_hash), std::to_string(fd), FILE_TO_FH);
  mem_client->put(std::to_string(fd), std::to_string(file_hash), FH_TO_FILE);
  mem_client->put(std::to_string(fd), mode, FH_TO_MODE);
  mem_client->put(std::to_string(fd), std::to_string(0), FH_TO_FP);
  return posix_api_response(OPERATION_SUCCESSFUL, fh);
}

posix_api_response posix_mdm::update_on_open(std::string filename, std::string mode) {
  if(filename.length() > MAX_FILENAME_LENGTH)
    return posix_api_response(METADATA_CREATION_FAILED);
  auto file_hash = hash_filename(filename);
  FILE* fh = fmemopen(NULL, 1, mode.c_str());
  int fd = fileno(fh);
  mem_client->put(std::to_string(file_hash), std::to_string(fd), FILE_TO_FH);
  mem_client->put(std::to_string(fd), std::to_string(file_hash), FH_TO_FILE);
  mem_client->put(std::to_string(fd), mode, FH_TO_MODE);
  if(mode =="r" || mode=="r+"){
    mem_client->put(std::to_string(fd), std::to_string(0), FH_TO_FP);
  }else if(mode =="w" || mode=="w+"){
    mem_client->replace(std::to_string(file_hash), std::to_string(0),
                     FILE_TO_SIZE);
    mem_client->put(std::to_string(fd), std::to_string(0), FH_TO_FP);
    void* chunkId=mem_client->get(std::to_string(file_hash),FILE_TO_CHUNK).data;
    if(chunkId!= nullptr){
      mem_client->put(std::to_string(file_hash), (char*)chunkId,
                      FILE_TO_INVALID_CHUNKID);
      mem_client->remove(std::to_string(file_hash),FILE_TO_CHUNK);
    }
  }else if(mode =="a" || mode=="a+"){
    mem_client->put(std::to_string(fd), std::to_string(get_filesize(filename)),
                    FH_TO_FP);
  }
  return posix_api_response(OPERATION_SUCCESSFUL, fh);
}

posix_api_response posix_mdm::update_on_close(int fildes) {
  mem_client->remove(std::to_string(get_filename(fildes)), FILE_TO_FH);
  mem_client->remove(std::to_string(fildes), FH_TO_MODE);
  mem_client->remove(std::to_string(fildes), FH_TO_FILE);
  mem_client->remove(std::to_string(fildes), FH_TO_FP);
  return posix_api_response(OPERATION_SUCCESSFUL);
}

posix_api_response posix_mdm::update_on_read(int fildes, size_t op_size) {
  mem_client->replace(std::to_string(fildes),
                      std::to_string(get_fp(fildes)+op_size), FH_TO_FP);
  return posix_api_response(OPERATION_SUCCESSFUL);
}

posix_api_response posix_mdm::update_on_write(int fildes, size_t op_size) {
  auto size = get_filesize(fildes);
  auto position = get_fp(fildes);
  if(position+op_size > size){
    mem_client->replace(std::to_string(fildes),
                        std::to_string(position+op_size), FILE_TO_SIZE);
  }
  mem_client->replace(std::to_string(fildes),
                      std::to_string(get_fp(fildes)+op_size), FH_TO_FP);
  return posix_api_response(OPERATION_SUCCESSFUL);
}

posix_api_response
posix_mdm::update_on_seek(int fildes, long int offset, int origin) {
  switch(origin){
    case SEEK_SET:
      mem_client->replace(std::to_string(fildes),
                          std::to_string(offset), FH_TO_FP);
      break;
    case SEEK_CUR:
      mem_client->replace(std::to_string(fildes),
                          std::to_string(get_fp(fildes)+offset), FH_TO_FP);
      break;
    case SEEK_END:
      mem_client->replace(std::to_string(fildes),
                          std::to_string(get_filesize(fildes)+offset), FH_TO_FP);
      break;
    default:
      fprintf(stderr, "Seek origin fault!\n");
      return posix_api_response(-1);
  }
  return posix_api_response(OPERATION_SUCCESSFUL);
}

posix_api_response posix_mdm::is_cached(posix_api_request request) {
  return posix_api_response();
}

std::vector<posix_api_response>
posix_mdm::get_file_location(posix_api_request request) {
  return std::vector<posix_api_response>();
}

void posix_mdm::update_mdm(posix_api_request request,posix_api_response &response) {
}
