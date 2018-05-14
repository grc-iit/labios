//
// Created by hdevarajan on 5/14/18.
//

#include <zconf.h>
#include "system_manager_service.h"
#include "../common/constants.h"

int system_manager_service::check_applications_score() {
    return 0;
}

int system_manager_service::run() {
    while(!kill) {
        sleep(SYSTEM_MANAGER_INTERVAL);
        check_applications_score();
    }
}
