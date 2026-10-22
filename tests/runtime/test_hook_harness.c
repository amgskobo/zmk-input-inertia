/* Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 *
 * The default of the test build's emit hook: a weak no-op that a test
 * overrides, and which must accept anything it is given.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#define __weak
#define ARG_UNUSED(x) ((void)(x))
struct device { int unused; };

/* DRIVER_FUNCTIONS */

int main(void) {
    static const struct device dev;
    inertia_test_before_emit(&dev, 0, false);
    inertia_test_before_emit(NULL, 7, true);
    puts("inertia default test hook: PASS");
    return 0;
}
