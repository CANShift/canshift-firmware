#ifndef CANSHIFT_LVGL_POOL_ALLOC_H
#define CANSHIFT_LVGL_POOL_ALLOC_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    size_t total;
    size_t free;
    size_t largest;
} CanshiftLvglPoolInfo;

void *canshift_lvgl_pool_alloc(size_t size);

void *canshift_lv_malloc(size_t size);
void canshift_lv_free(void *ptr);
void *canshift_lv_realloc(void *ptr, size_t size);
bool canshift_lvgl_pool_info(CanshiftLvglPoolInfo *out);

#ifdef __cplusplus
}
#endif

#endif
