// Tiny HTTP control API for Home Assistant (rest_command). Non-blocking:
// http_poll() is called from the main loop and handles at most one request.
//
//   GET|POST /cat/start[?s=SECONDS]   cat mode (connects to the saved car first)
//   GET|POST /cat/stop                stop cat mode (car stops)
//   GET|POST /stop                    same as /cat/stop
//   GET      /status                  JSON status
// With a token in config.json every request needs ?token=... (or &token=...).
#pragma once
#include <stdbool.h>

typedef enum {
	HTTP_STATUS,
	HTTP_CAT_START,
	HTTP_CAT_STOP,
	HTTP_UNKNOWN,
} HttpCommand;

typedef struct {
	HttpCommand cmd;
	int seconds; // 0 = default
	bool authorized;
	char path[64];
} HttpRequest;

bool http_start(int port);
void http_stop(void);
bool http_running(void);
// Returns true when a request was read; then call http_respond exactly once.
bool http_poll(HttpRequest *req, const char *token);
void http_respond(int code, const char *json);
// "192.168.1.23" or "" if not connected to a network.
void http_local_ip(char out[16]);
