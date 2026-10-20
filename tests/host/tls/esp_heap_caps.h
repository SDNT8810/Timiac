#pragma once
#include <stddef.h>
#include <stdint.h>
constexpr unsigned MALLOC_CAP_INTERNAL = 1, MALLOC_CAP_8BIT = 2;
uint32_t heap_caps_get_free_size(unsigned caps);
uint32_t heap_caps_get_largest_free_block(unsigned caps);
void* heap_caps_calloc(size_t count, size_t size, unsigned caps);
void heap_caps_free(void* pointer);
