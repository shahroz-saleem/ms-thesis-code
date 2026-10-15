#ifndef PRASLE_H
#define PRASLE_H

#include <stdint.h>

#define PRASLE_MAX_NEIGHBORS    8

void prasle_init(void);
int prasle_setup(uint16_t rank, unsigned k_rounds, unsigned round_ms,
                 const uint16_t *neighbors, unsigned count);
void prasle_start(void);

#endif
