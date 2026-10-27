#pragma once
#include "stdafx.h"
#include <iostream>

namespace Properties {

    void Render();

    extern std::string targetObjectId;
    extern float positionX;
    extern float positionY;
    extern float positionZ;
    extern float rotationX;
    extern float rotationY;
    extern float rotationZ;
    extern bool isModel;
    extern bool isPrevievPaste;
    extern int itemId;
    extern int groupId;
    extern int prototypeId;
    extern std::string objectType;

}