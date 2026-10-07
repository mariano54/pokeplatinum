#include "applications/poketch/poketch_map.h"

#include <nitro.h>

static const u16 mapPositionsX[] = {
    26,
    32,
    38,
    44,
    50,
    56,
    62,
    68,
    74,
    80,
    86,
    92,
    98,
    104,
    110,
    116,
    122,
    128,
    134,
    140,
    146,
    152,
    158,
    164,
    170,
    176,
    182,
    188,
    194,
    200
};

static const u16 mapPositionsY[] = {
    0,
    0,
    0,
    0,
    0,
    24,
    30,
    36,
    42,
    48,
    54,
    60,
    66,
    72,
    78,
    84,
    90,
    96,
    102,
    108,
    114,
    120,
    126,
    132,
    138,
    144,
    150,
    156,
    162,
    168,
    174,
    180,
    186
};

void PoketchMap_GetPositionOnMap(u32 x, u32 y, u32 *mapX, u32 *mapY)
{
    GF_ASSERT(x < NELEMS(mapPositionsX));
    GF_ASSERT(y < NELEMS(mapPositionsY));

    if (x >= NELEMS(mapPositionsX)) {
        x = 0;
    }

    if (y >= NELEMS(mapPositionsY)) {
        y = 0;
    }

    *mapX = mapPositionsX[x];
    *mapY = mapPositionsY[y];
}

void PoketchMap_GetHiddenLocationPosition(int hiddenLocation, u32 *mapX, u32 *mapY)
{
    static const struct {
        u32 x;
        u32 y;
    } hiddenLocationPositions[] = {
        { 32, 42 },
        { 50, 42 },
        { 168, 122 },
        { 194, 58 },
    };

    GF_ASSERT(hiddenLocation < NELEMS(hiddenLocationPositions));

    *mapX = hiddenLocationPositions[hiddenLocation].x;
    *mapY = hiddenLocationPositions[hiddenLocation].y;
}

BOOL PoketchMap_GetPositionFromMapID(enum MapHeaderID mapID, u32 *x, u32 *y)
{
    static const struct {
        u16 mapID;
        u8 x;
        u8 y;
    } positions[] = {
        { MAP_HEADER_ROUTE_201, 47, 150 },
        { MAP_HEADER_ROUTE_202, 56, 144 },
        { MAP_HEADER_ROUTE_203, 65, 132 },
        { MAP_HEADER_ROUTE_204_SOUTH, 50, 126 },
        { MAP_HEADER_ROUTE_204_NORTH, 50, 120 },
        { MAP_HEADER_ROUTE_205_SOUTH, 62, 108 },
        { MAP_HEADER_ROUTE_205_NORTH, 74, 90 },
        { MAP_HEADER_ROUTE_206, 80, 111 },
        { MAP_HEADER_ROUTE_207, 83, 126 },
        { MAP_HEADER_ROUTE_208, 101, 126 },
        { MAP_HEADER_ROUTE_209, 125, 126 },
        { MAP_HEADER_ROUTE_210_SOUTH, 128, 102 },
        { MAP_HEADER_ROUTE_210_NORTH, 122, 90 },
        { MAP_HEADER_ROUTE_211_WEST, 92, 90 },
        { MAP_HEADER_ROUTE_211_EAST, 104, 90 },
        { MAP_HEADER_ROUTE_212_NORTH, 110, 138 },
        { MAP_HEADER_ROUTE_212_SOUTH, 119, 150 },
        { MAP_HEADER_ROUTE_213, 152, 147 },
        { MAP_HEADER_ROUTE_214, 152, 120 },
        { MAP_HEADER_ROUTE_215, 140, 102 },
        { MAP_HEADER_ROUTE_216, 86, 66 },
        { MAP_HEADER_ROUTE_217, 80, 51 },
        { MAP_HEADER_ROUTE_218, 41, 132 },
        { MAP_HEADER_ROUTE_219, 56, 156 },
        { MAP_HEADER_ROUTE_220, 59, 162 },
        { MAP_HEADER_ROUTE_221, 74, 162 },
        { MAP_HEADER_ROUTE_222, 170, 138 },
        { MAP_HEADER_VALLEY_WINDWORKS_OUTSIDE, 68, 114 },
        { MAP_HEADER_FUEGO_IRONWORKS_OUTSIDE, 56, 102 }
    };

    for (int i = 0; i < NELEMS(positions); i++) {
        if (positions[i].mapID == mapID) {
            *x = positions[i].x;
            *y = positions[i].y;
            return TRUE;
        }
    }

    return FALSE;
}
