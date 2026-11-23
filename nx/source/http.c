#include "http.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <switch.h>

#include "log.h"

static int g_listen = -1;
static int g_client = -1;
static bool g_nifm;

bool http_start(int port) {
	if (port <= 0)
		return false;
	g_nifm = R_SUCCEEDED(nifmInitialize(NifmServiceType_User));
	g_listen = socket(AF_INET, SOCK_STREAM, 0);
	if (g_listen < 0) {
		log_msg("http: socket errno=%d", errno);
		return false;
	}
	int one = 1;
	setsockopt(g_listen, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
	struct sockaddr_in a;
	memset(&a, 0, sizeof(a));
	a.sin_family = AF_INET;
	a.sin_port = htons((uint16_t)port);
	a.sin_addr.s_addr = htonl(INADDR_ANY);
	if (bind(g_listen, (struct sockaddr *)&a, sizeof(a)) < 0 || listen(g_listen, 2) < 0) {
		log_msg("http: bind/listen port %d errno=%d", port, errno);
		close(g_listen);
		g_listen = -1;
		return false;
	}
	fcntl(g_listen, F_SETFL, fcntl(g_listen, F_GETFL, 0) | O_NONBLOCK);
	log_msg("http: nasluch na porcie %d", port);
	return true;
}

void http_stop(void) {
	if (g_client >= 0)
		close(g_client);
	if (g_listen >= 0)
		close(g_listen);
	g_client = g_listen = -1;
	if (g_nifm)
		nifmExit();
	g_nifm = false;
}

bool http_running(void) {
	return g_listen >= 0;
}

void http_local_ip(char out[16]) {
	out[0] = 0;
	u32 ip = 0;
	if (g_nifm && R_SUCCEEDED(nifmGetCurrentIpAddress(&ip)) && ip)
		snprintf(out, 16, "%u.%u.%u.%u", ip & 0xff, (ip >> 8) & 0xff, (ip >> 16) & 0xff, ip >> 24);
}

static const char *query_param(const char *query, const char *key, char *out, size_t n) {
	size_t klen = strlen(key);
	for (const char *p = query; p && *p;) {
		if (!strncmp(p, key, klen) && p[klen] == '=') {
			p += klen + 1;
			size_t k = 0;
			while (*p && *p != '&' && *p != ' ' && k < n - 1)
				out[k++] = *p++;
			out[k] = 0;
			return out;
		}
		p = strchr(p, '&');
		if (p)
			p++;
	}
	return NULL;
}

bool http_poll(HttpRequest *req, const char *token) {
	if (g_listen < 0 || g_client >= 0)
		return false;
	int c = accept(g_listen, NULL, NULL);
	if (c < 0)
		return false;
	// Short receive timeout: HA sends the whole request at once.
	struct timeval tv = {0, 300000};
	setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
	char buf[1024];
	ssize_t n = recv(c, buf, sizeof(buf) - 1, 0);
	if (n <= 0) {
		close(c);
		return false;
	}
	buf[n] = 0;
	g_client = c;

	memset(req, 0, sizeof(*req));
	req->cmd = HTTP_UNKNOWN;
	// "GET /cat/start?s=60&token=x HTTP/1.1"
	char *sp = strchr(buf, ' ');
	char *target = sp ? sp + 1 : buf;
	char *end = strchr(target, ' ');
	if (end)
		*end = 0;
	char *q = strchr(target, '?');
	if (q)
		*q++ = 0;
	snprintf(req->path, sizeof(req->path), "%.63s", target);

	char val[40];
	req->authorized = !token[0] || (q && query_param(q, "token", val, sizeof(val)) && !strcmp(val, token));
	if (q && query_param(q, "s", val, sizeof(val)))
		req->seconds = atoi(val);
	if (!strcmp(req->path, "/cat/start"))
		req->cmd = HTTP_CAT_START;
	else if (!strcmp(req->path, "/cat/stop") || !strcmp(req->path, "/stop"))
		req->cmd = HTTP_CAT_STOP;
	else if (!strcmp(req->path, "/status") || !strcmp(req->path, "/"))
		req->cmd = HTTP_STATUS;
	log_msg("http: %s%s", req->path, req->authorized ? "" : " (zly token)");
	return true;
}

void http_respond(int code, const char *json) {
	if (g_client < 0)
		return;
	const char *reason = code == 200 ? "OK" : code == 403 ? "Forbidden" : code == 404 ? "Not Found"
	                                                                                  : "Conflict";
	char head[160];
	int hl = snprintf(head, sizeof(head),
	                  "HTTP/1.1 %d %s\r\nContent-Type: application/json\r\nContent-Length: %u\r\n"
	                  "Connection: close\r\n\r\n",
	                  code, reason, (unsigned)strlen(json));
	send(g_client, head, hl, 0);
	send(g_client, json, strlen(json), 0);
	close(g_client);
	g_client = -1;
}
