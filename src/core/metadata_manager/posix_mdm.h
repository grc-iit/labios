//
// Created by anthony on 5/11/17.
//

#ifndef PORUS_POSIX_MDM_H
#define PORUS_POSIX_MDM_H

#include <memory>
#include "abstract_mdm.h"
#include "../../common/components/api_response.h"
#include "../../common/components/enumerators.h"
#include "../cache_manager/memcached_client.h"
#include <vector>

class posix_mdm : public abstract_mdm {
private:
  const std::string FILENAME="F";
  const std::string SIZE="S";
  const std::string FILE_DESCRIPTOR="D";
  const std::string MODE="M";
  const std::string FILE_POINTER="P";
  const std::string BLOCK="B";
  const std::string IS_CACHED="C";
  const std::string REPLICA="R";
  const std::string INVALID="I";
  const std::string FILE_TO_SIZE = FILENAME+KEY_SPACE_SEPARATOR+SIZE;
  const std::string FILE_TO_FH = FILENAME+KEY_SPACE_SEPARATOR+FILE_DESCRIPTOR;
  const std::string FH_TO_FILE = FILE_DESCRIPTOR+KEY_SPACE_SEPARATOR+FILENAME;
  const std::string FH_TO_MODE = FILE_DESCRIPTOR+KEY_SPACE_SEPARATOR+MODE;
  const std::string FH_TO_FP = FILE_DESCRIPTOR+KEY_SPACE_SEPARATOR+FILE_POINTER;
  const std::string FILE_TO_CHUNK = FILENAME+KEY_SPACE_SEPARATOR+BLOCK;
  const std::string CHUNKID_TO_CHUNK = BLOCK;
  const std::string CHUNKID_TO_IS_CACHED = BLOCK+KEY_SPACE_SEPARATOR+IS_CACHED;
  const std::string CHUNKID_TO_REPLICA = BLOCK+KEY_SPACE_SEPARATOR+REPLICA;
  const std::string REPLICAID_TO_REPLICA = REPLICA;
  const std::string FILE_TO_INVALID_CHUNKID =
      FILENAME+KEY_SPACE_SEPARATOR+BLOCK+KEY_SPACE_SEPARATOR+INVALID;
/******************************************************************************
*Variables and members
******************************************************************************/
  static std::shared_ptr<posix_mdm> instance;
  std::shared_ptr<memcached_client> mem_client;
/******************************************************************************
*Constructor
******************************************************************************/
  posix_mdm():abstract_mdm(){
    mem_client=memcached_client::getInstance();
  }
/******************************************************************************
*Functions
******************************************************************************/
  uint32_t hash_filename(std::string filename);
public:
/******************************************************************************
*Interface
******************************************************************************/
  int get_file_descriptor(FILE* fh);
  uint32_t get_filename(int fildes);
  size_t get_filesize(std::string filename);
  bool is_created(std::string filename);
  bool is_opened(std::string filename);
  bool is_opened(int fildes);
  bool check_mode(int fildes, std::string mode);
  std::string get_mode(int fildes);
  size_t get_filesize(int fildes);
  long get_fp(int fildes);


  posix_api_response create(std::string filename, std::string mode);
  posix_api_response update_on_open(std::string filename, std::string mode);
  posix_api_response update_on_close(int fildes);
  posix_api_response update_on_read(int fildes, size_t op_size);
  posix_api_response update_on_write(int fildes, size_t op_size);
  posix_api_response update_on_seek(int fildes, long int offset, int origin);
  posix_api_response is_cached(posix_api_request request);
  std::vector<posix_api_response> get_file_location(posix_api_request request);
  void update_mdm(posix_api_request request,posix_api_response &response);

/******************************************************************************
*Getters and setters
******************************************************************************/
  inline static std::shared_ptr<posix_mdm> getInstance(){
    return instance== nullptr ? instance = std::shared_ptr<posix_mdm>
        (new posix_mdm()) : instance;
  }
/******************************************************************************
*Destructor
******************************************************************************/
  virtual ~posix_mdm(){}
};


#endif //PORUS_POSIX_MDM_H
