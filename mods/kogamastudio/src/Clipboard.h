#pragma once
#include "stdafx.h"
#include <iostream>

namespace Clipboard {

    void Render();

    extern int cubeCount;
    extern int offsetX;
    extern int offsetY;
    extern int offsetZ;
    extern int rotationX;
    extern int rotationY;
    extern int rotationZ;
    extern bool mirrorX;
    extern bool mirrorY;
    extern bool mirrorZ;
    extern int clipMinX, clipMaxX;
    extern int clipMinY, clipMaxY;
    extern int clipMinZ, clipMaxZ;
}