#pragma once
#include "../core/common.h"

namespace offsets {
    bool Load();
    void* Fn(const char* key);
    int Field(const char* key);
}
