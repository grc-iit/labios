/*******************************************************************************
* Created by akougkas on 6/26/2018
* Updated by
* Illinois Institute of Technology - SCS Lab
* (C) 2018
******************************************************************************/
#ifndef AETRIO_RETURN_CODES_H
#define AETRIO_RETURN_CODES_H
/******************************************************************************
*error code enum
******************************************************************************/
typedef enum return_codes{
    SUCCESS                            = 7777,

    /* Error codes*/
    WORKER__SETTING_DIR_FAILED         = 7800,
    WORKER__UPDATE_CAPACITY_FAILED     = 7801,
    WORKER__UPDATE_SCORE_FAILED        = 7802,
    MDM__CREATE_FAILED                 = 7803,
    MDM__FILENAME_MAX_LENGTH           = 7804,
    LIB__FCLOSE_FAILED                 = 7805,
    MDM__UPDATE_ON_FSEEK_FAILED        = 7806,
    UPDATE_FILE_POINTER_FAILED         = 7807,
    PREFETCH_ENGINE_FAILED             = 7808,
    FILE_SEEK_FAILED                   = 7809,
    FH_DOES_NOT_EXIST                  = 7810,
    FILE_READ_FAILED                   = 7811,
    FILE_WRITE_FAILED                  = 7812,

    CONTAINER_NOT_VALID                = 7821,
    OBJECT_NOT_FOUND                   = 7822,



    NO_CACHED_DATA_FOUND               = 7950
} returncode;



#endif //AETRIO_RETURN_CODES_H
