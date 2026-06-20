#pragma once
#include "../../types.h"

bool        isDown(int pin);
ButtonEvent readButtonEvent(int pin);
bool        handleIdleRec();
