#pragma once
#include <stdnoreturn.h>

void panic(const char* msg);
void panicf(const char* fmt, ...);

