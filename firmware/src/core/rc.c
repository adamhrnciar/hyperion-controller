// SPDX-License-Identifier: GPL-3.0-or-later
#include "rc.h"

void rc_unpack_11bit(const uint8_t *src, uint16_t *dst, int n) {
    uint32_t bits = 0;
    int nbits = 0;
    int ch = 0;
    while (ch < n) {
        while (nbits < 11) {
            bits |= (uint32_t)(*src++) << nbits;
            nbits += 8;
        }
        dst[ch++] = (uint16_t)(bits & 0x7FFu);
        bits >>= 11;
        nbits -= 11;
    }
}
