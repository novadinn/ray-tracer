#pragma once

#include <assert.h>

#define VK_CHECK(result)                                                       \
  { assert(result == VK_SUCCESS); }
