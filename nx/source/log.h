// Thread-safe log: sdmc:/switch/shellrc/shellrc.log (+ stderr, which nxlink -s
// shows on the PC when launched over the network).
#pragma once

#define LOG_PATH "sdmc:/switch/shellrc/shellrc.log"

void log_open(void);
void log_close(void);
void log_msg(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
// Seconds since log_open.
double log_time(void);
