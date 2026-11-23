// Rolling latency/period statistics. Portable C99.
#pragma once
#include <stdint.h>

#define STATS_CAP 1024

typedef struct {
	float v[STATS_CAP];
	uint32_t n;    // total samples ever pushed
	uint32_t head; // next write position
} Rolling;

typedef struct {
	uint32_t n;
	float min, p50, p95, p99, max;
} Summary;

void rolling_push(Rolling *r, float v);
// Summary of the last min(n, STATS_CAP) samples. n = 0 when empty.
Summary rolling_summary(const Rolling *r);
