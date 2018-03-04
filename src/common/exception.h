//
// Created by hariharan on 3/3/18.
//

#ifndef PORUS_MAIN_EXCEPTION_H
#define PORUS_MAIN_EXCEPTION_H

#include <stdexcept>

class NotImplementedException : public std::logic_error
{
public:
    NotImplementedException(const std::string &__arg) : logic_error(__arg) {}

    virtual char const * what()  const noexcept override { return "Function not yet implemented."; }
};
#endif //PORUS_MAIN_EXCEPTION_H
