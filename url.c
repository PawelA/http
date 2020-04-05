#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "http.h"

struct url_info {
	char *scheme;
	char *host;
	char *path;
};

int parse_url(char *str, struct url_info *url) {
	url->scheme = str;
	str = strchr(str, ':');
	if (!str) return 0;

	*str = 0;
	url->host = str + 1;
	if (str[1] != '/' || str[2] != '/') return 0;
	str += 3;

	while (*str && *str != '/') {
		*(str - 2) = *str;
		str++;
	}

	*(str - 2) = 0;
	if (!*str) {
		str--;
		*str = '/';
	}
	url->path = str;

	return 1;
}

int main(int argc, char **argv) {
	struct url_info url;
	struct http_client http;
	struct http_request req;
	struct http_response resp;
	int secure;
	int res;

	if (argc != 2) {
		printf("usage: %s <url>\n", argv[0]);
		exit(1);
	}

	if (!parse_url(argv[1], &url)) {
		fprintf(stderr, "url must be of the form <scheme>://<host>[<path>]\n");
		exit(1);
	}

	if (strcmp(url.scheme, "http") == 0)
		secure = 0;
	else if (strcmp(url.scheme, "https") == 0)
		secure = 1;
	else {
		fprintf(stderr, "url scheme must either be http of https\n");
		exit(1);
	}

	http_init();
	res = http_connect(&http, url.host, secure);

	if (res != 0) {
		fprintf(stderr, "couldn't connect\n");
		exit(1);
	}

	req.host = url.host;
	req.path = url.path;
	req.agent = NULL;
	req.referer = NULL;
	req.cookie = NULL;
	req.data = NULL;
	req.data_len = 0;

	http_send(&http, &req);
	if (http_recv(&http, &resp) != 1) exit(1);

	fwrite(resp.body, 1, resp.body_len, stdout);

	free(resp.buff);
	http_disconnect(&http);
	http_uninit();

	exit(0);
}
