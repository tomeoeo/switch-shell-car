#include "log.h"

#include <stdarg.h>
#include <stdio.h>
#include <sys/stat.h>
#include <switch.h>

static FILE *g_file;
static Mutex g_lock;
static u64 g_t0;

void log_open(void) {
	mutexInit(&g_lock);
	g_t0 = armGetSystemTick();
	mkdir("sdmc:/switch", 0777);
	mkdir("sdmc:/switch/shellrc", 0777);
	g_file = fopen(LOG_PATH, "w");
}

double log_time(void) {
	return (double)armTicksToNs(armGetSystemTick() - g_t0) / 1e9;
}

void log_msg(const char *fmt, ...) {
	char line[256];
	va_list a;
	va_start(a, fmt);
	vsnprintf(line, sizeof(line), fmt, a);
	va_end(a);
	mutexLock(&g_lock);
	double t = log_time();
	if (g_file) {
		fprintf(g_file, "%9.3f %s\n", t, line);
		fflush(g_file);
	}
	fprintf(stderr, "%9.3f %s\n", t, line);
	mutexUnlock(&g_lock);
}

void log_close(void) {
	mutexLock(&g_lock);
	if (g_file)
		fclose(g_file);
	g_file = NULL;
	mutexUnlock(&g_lock);
}
