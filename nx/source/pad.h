#pragma once
#include <switch.h>

#include "../../core/input.h"

// All controllers OR-ed together (handheld, detached Joy-Cons, Pro Controller).
void pad_init(PadState *p);
void pad_read(PadState *p, PadInput *out);
