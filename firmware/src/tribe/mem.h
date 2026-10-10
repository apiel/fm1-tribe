/* SPDX-License-Identifier: GPL-3.0-only */
/* memset / memcpy: the host has <string.h>; the device has no libc but libc.c defines these. */
#pragma once
#ifdef X0X_HOST
#include <string.h>
#else
void *memset(void *d, int c, unsigned n);
void *memcpy(void *d, const void *s, unsigned n);
#endif
