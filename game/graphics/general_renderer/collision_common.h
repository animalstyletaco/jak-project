#pragma once
#include <vector>

namespace collision {
const int PAT_MOD_COUNT = 3;
const int PAT_EVT_COUNT = 7;
const int PAT_MAT_COUNT = 23;

const static std::vector<float> material_colors_jak1 = {
    1.0f,  0.7f,  1.0f,   // 0, stone
    0.1f,  2.0f,  2.0f,   // 1, ice
    0.75f, 0.25f, 0.1f,   // 2, quicksand
    0.1f,  0.25f, 0.75f,  // 3, waterbottom
    0.5f,  0.15f, 0.1f,   // 4, tar
    2.0f,  1.5f,  0.5f,   // 5, sand
    1.5f,  0.75f, 0.1f,   // 6, wood
    0.1f,  1.35f, 0.1f,   // 7, grass
    1.7f,  1.3f,  0.1f,   // 8, pcmetal
    1.8f,  1.8f,  1.8f,   // 9, snow
    1.5f,  0.2f,  1.0f,   // 10, deepsnow
    1.2f,  0.5f,  0.3f,   // 11, hotcoals
    1.4f,  0.1f,  0.1f,   // 12, lava
    0.8f,  0.3f,  0.1f,   // 13, crwood
    1.0f,  0.4f,  1.0f,   // 14, gravel
    1.5f,  0.5f,  0.15f,  // 15, dirt
    0.7f,  0.7f,  1.0f,   // 16, metal
    0.1f,  0.1f,  1.2f,   // 17, straw
    0.75f, 1.75f, 0.75f,  // 18, tube
    0.4f,  0.1f,  0.8f,   // 19, swamp
    0.1f,  0.4f,  0.8f,   // 20, stopproj
    1.9f,  0.1f,  1.9f,   // 21, rotate
    1.0f,  1.0f,  1.0f,   // 22, neutral
};

const static std::vector<float> event_colors_jak1 = {
    1.0f, 1.0f, 1.0f,  // 0, none
    0.2f, 1.0f, 1.0f,  // 1, deadly
    0.1f, 1.0f, 0.1f,  // 2, endlessfall
    1.0f, 1.0f, 0.1f,  // 3, burn
    0.1f, 0.1f, 1.0f,  // 4, deadlyup
    1.0f, 0.1f, 0.5f,  // 5, burnup
    1.0f, 0.1f, 0.1f,  // 6, melt
};

const static std::vector<float> mode_colors_jak1 = {
    1.25f, 0.1f, 0.1f,  // 0, ground
    0.1f,  0.1f, 1.0f,  // 1, wall
    1.0f,  0.1f, 1.0f,  // 2, obstacle
};

const static std::vector<float> material_colors_jak2 = {
    1.0f,  0.1f,  1.0f,   // 0, unknown
    0.1f,  2.0f,  2.0f,   // 1, ice
    0.75f, 0.25f, 0.1f,   // 2, quicksand
    0.1f,  0.25f, 0.75f,  // 3, waterbottom
    0.5f,  0.15f, 0.1f,   // 4, tar
    2.0f,  1.5f,  0.5f,   // 5, sand
    1.5f,  0.75f, 0.1f,   // 6, wood
    0.1f,  1.35f, 0.1f,   // 7, grass
    1.7f,  1.3f,  0.1f,   // 8, pcmetal
    1.8f,  1.8f,  1.8f,   // 9, snow
    1.5f,  0.2f,  1.0f,   // 10, deepsnow
    1.2f,  0.5f,  0.3f,   // 11, hotcoals
    1.4f,  0.1f,  0.1f,   // 12, lava
    0.8f,  0.3f,  0.1f,   // 13, crwood
    1.0f,  0.4f,  1.0f,   // 14, gravel
    1.5f,  0.5f,  0.15f,  // 15, dirt
    0.7f,  0.7f,  1.0f,   // 16, metal
    0.1f,  0.1f,  1.2f,   // 17, straw
    0.75f, 1.75f, 0.75f,  // 18, tube
    0.4f,  0.1f,  0.8f,   // 19, swamp
    0.1f,  0.4f,  0.8f,   // 20, stopproj
    1.9f,  0.1f,  1.9f,   // 21, rotate
    1.0f,  1.0f,  1.0f,   // 22, neutral
    1.0f,  0.7f,  1.0f,   // 23, stone
    0.8f,  1.2f,  1.2f,   // 24, crmetal
    0.7f,  0.0f,  0.0f,   // 25, carpet
    0.1f,  0.9f,  0.1f,   // 26, grmetal
    1.4f,  0.7f,  0.1f,   // 27, shmetal
    0.5f,  0.5f,  0.0f,   // 28, hdwood
};

const static std::vector<float> event_colors_jak2 = {
    1.0f, 1.0f, 1.0f,  // 0, none
    0.5f, 1.0f, 1.0f,  // 1, deadly
    0.1f, 1.0f, 0.1f,  // 2, endlessfall
    1.0f, 1.0f, 0.1f,  // 3, burn
    0.1f, 0.1f, 1.0f,  // 4, deadlyup
    1.0f, 0.1f, 0.5f,  // 5, burnup
    1.0f, 0.1f, 0.1f,  // 6, melt
    0.1f, 0.7f, 0.7f,  // 7, slide
    1.0f, 0.2f, 1.0f,  // 8, lip
    0.5f, 0.2f, 1.0f,  // 9, lipramp
    0.1f, 0.5f, 1.0f,  // 10, shock
    0.5f, 0.6f, 1.0f,  // 11, shockup
    0.5f, 0.6f, 0.5f,  // 12, hide
    0.5f, 1.0f, 0.5f,  // 13, rail
    0.7f, 0.7f, 0.7f,  // 14, slippery
};

const static std::vector<float> mode_colors_jak2 = {
    1.25f, 0.1f, 0.1f,  // 0, ground
    0.1f,  0.1f, 1.0f,  // 1, wall
    1.0f,  0.1f, 1.0f,  // 2, obstacle
    1.0f,  1.0f, 0.1f,  // 3, halfpipe
};
}  // namespace collision
