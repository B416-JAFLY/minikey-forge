#pragma once
#include <stdint.h>
#include <stddef.h>
typedef struct {const uint8_t *p; size_t n;} cv;
typedef struct {uint8_t *p; size_t n, cap; int error;} cw;
int cb_valid(cv v);
int cb_head(cv v, unsigned *type, uint32_t *value, size_t *head);
int cb_get(cv map, int key, const char *text, cv *out);
int cb_at(cv array, unsigned index, cv *out);
int cb_int(cv v, int *out);
int cb_bytes(cv v, unsigned type, const uint8_t **p, size_t *n);
int cb_equal(cv v, const char *text);
void cb_put(cw *w, unsigned type, uint32_t value);
void cb_raw(cw *w, const void *p, size_t n);
void cb_blob(cw *w, const void *p, size_t n);
void cb_text(cw *w, const char *text);
void cb_bool(cw *w, int value);
