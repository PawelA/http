#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "http.h"

int main() {
	struct http_client client;
	struct http_request req;
	struct http_response resp;

	http_init();

	if (http_connect(&client, "en.wikipedia.org", 1) == -1) {
		fprintf(stderr, "couldn't connect\n");
		return 1;
	}

	req.path = "/wiki/Hypertext_Transfer_Protocol";
	req.host = "en.wikipedia.org";
	req.agent = NULL;
	req.referer = NULL;
	req.cookie = NULL;
	req.data = NULL;
	req.data_len = 0;

	if (http_send(&client, &req) == -1) 
		return 1;
	if (http_recv(&client, &resp) == -1)
		return 1;

	fwrite(resp.body, 1, resp.body_len, stdout);

	free(resp.buff);
	http_disconnect(&client);
	http_uninit();

	return 0;
}
