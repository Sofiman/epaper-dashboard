#pragma once

#include <stdio.h>

#define ESP_LOGI(Tag, Fmt, ...) printf("[%s] " Fmt, Tag __VA_OPT__(,) __VA_ARGS__);
