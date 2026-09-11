// Pch.h — port shim for the vendored Casso SSI-263 engine.
//
// The engine (Ssi263.h / Ssi263.cpp) originally pulled in a Windows
// precompiled header. On the RP2040 it needs only the STL headers it actually
// uses plus the `Byte` typedef; nothing else from the Casso project.
#pragma once

#include <cstdint>
#include <cmath>
#include <numbers>
#include <algorithm>

using Byte = uint8_t;
