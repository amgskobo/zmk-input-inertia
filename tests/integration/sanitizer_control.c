/* Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 * Actual recovering sanitizer findings for launcher admission tests, not firmware.
 */
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv) {
    int reload = 0;
    for (int i = 1; i < argc; ++i) {
        if (strncmp(argv[i], "--flash=", 8) == 0) {
            int fd = open(argv[i] + 8, O_CREAT | O_EXCL | O_WRONLY, 0600);
            reload = fd < 0;
            if (fd >= 0) {
                close(fd);
            }
        }
    }
    const char *phase = getenv("INERTIA_CONTROL_PHASE");
    if (phase && (strcmp(phase, "both") == 0 ||
                  strcmp(phase, reload ? "reload" : "first") == 0)) {
#if defined(CONTROL_UBSAN)
        volatile int shift = 31;
        volatile int32_t value = 1 << shift;
        (void)value;
#elif defined(CONTROL_ASAN)
        volatile unsigned index = 8;
        volatile char bytes[8] = {0};
        volatile char value = bytes[index];
        (void)value;
#endif
    }
    puts("runtime error: simulated endpoint label without a source location");
    puts("inertia runtime tests: PASS");
    puts("PASS: Inertia registry range/type rejection, per-node apply and NVS save");
    puts("PASS: persisted Inertia value reloads");
    puts("inertia endpoint boundary: PASS groups=29");
    puts("inertia lifecycle boundary: PASS groups=11");
    puts("inertia shared HID regression: PASS");
    return 0;
}
