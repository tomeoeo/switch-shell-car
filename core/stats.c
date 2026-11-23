#include "stats.h"

#include <stdlib.h>
#include <string.h>

void rolling_push(Rolling *r, float v) {
	r->v[r->head] = v;
	r->head = (r->head + 1) % STATS_CAP;
	r->n++;
}

static int cmp_float(const void *a, const void *b) {
	float x = *(const float *)a, y = *(const float *)b;
	return x < y ? -1 : x > y;
}

static float at(const float *s, uint32_t n, int pct) {
	// Nearest rank, same as protocol/stats.ts.
	uint32_t rank = (uint32_t)((pct * n + 99) / 100);
	if (rank < 1)
		rank = 1;
	if (rank > n)
		rank = n;
	return s[rank - 1];
}

Summary rolling_summary(const Rolling *r) {
	Summary s = {0};
	uint32_t n = r->n < STATS_CAP ? r->n : STATS_CAP;
	if (n == 0)
		return s;
	static float sorted[STATS_CAP]; // single-threaded use per call site
	memcpy(sorted, r->v, n * sizeof(float));
	qsort(sorted, n, sizeof(float), cmp_float);
	s.n = n;
	s.min = sorted[0];
	s.p50 = at(sorted, n, 50);
	s.p95 = at(sorted, n, 95);
	s.p99 = at(sorted, n, 99);
	s.max = sorted[n - 1];
	return s;
}
