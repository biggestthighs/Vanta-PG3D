#pragma once
#include "config.h"

namespace atmosphere {
    struct Palette {
        const char* name;
        const char* description;
        Col zenith, horizon, aurora, mist, sparks;
    };
    const Palette& Colors(int index);
    void Panorama(std::vector<uint8_t>& pixels, int width, int height, int palette, bool stars);
    void Preview(ImDrawList& draw, ImVec2 origin, ImVec2 size, const WorldCfg& settings, double time);
}
