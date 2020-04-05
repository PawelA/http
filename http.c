#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <openssl/ssl.h>

#include "http.h"

#define READ_BUFF_SIZE 1024

enum {
	STATUS_LINE,
	STATUS_LINE_LF,
	HEADERS,
	HEADERS1,
	HEADERS2,
	HEADERS3,
	CHUNK_SIZE,
	CHUNK_SIZE_LF,
	CHUNK,
	CHUNK_CRLF,
	CHUNK_LAST,
	LENGTH,
	CLOSE,
	DONE,
};

int save_byte[] = {
	[STATUS_LINE] = 1,
	[STATUS_LINE_LF] = 1,
	[HEADERS] = 1,
	[HEADERS1] = 1,
	[HEADERS2] = 1,
	[HEADERS3] = 1,
	[CHUNK] = 1,
	[LENGTH] = 1,
	[CLOSE] = 1,
};

static SSL_CTX *ssl_ctx;



void http_init() {
	SSL_library_init();
	ssl_ctx = SSL_CTX_new(SSLv23_client_method());
}

void http_uninit() {
	SSL_CTX_free(ssl_ctx);
}

int http_connect(struct http_client *client, const char *host, int secure) {
	struct addrinfo hints;
	struct addrinfo *res;

	memset(client, 0, sizeof(struct http_client));
	memset(&hints, 0, sizeof(struct addrinfo));

	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	if (getaddrinfo(host, secure ? "443" : "80", &hints, &res) != 0)
		return -1;

	client->sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
	if (client->sockfd < 0)
		return -1;

	if (connect(client->sockfd, res->ai_addr, res->ai_addrlen) < 0) 
		return -1;

	freeaddrinfo(res);

	if (secure) {
		client->ssl = SSL_new(ssl_ctx);
		if (!client->ssl)
			return -1;

		SSL_set_fd(client->ssl, client->sockfd);

		if (SSL_connect(client->ssl) != 1)
			return -1;
	}
		
	return 0;
}

void http_disconnect(struct http_client *client) {
	if (client->ssl) {
		SSL_shutdown(client->ssl);
		SSL_free(client->ssl);
	}
	close(client->sockfd);
}

static int chunk_encoding(const char *headers) {
	return !!strstr(headers, "Transfer-Encoding: chunked");
}

static int length_encoding(const char *headers) {
	const char *found;
	const char *content = "Content-Length: ";
	int res;
		
	found = strstr(headers, content);
	if (!found)
		return -1;

	found += strlen(content);
	if (sscanf(found, "%d", &res) != 1)
		return -1;

	return res;
}

static int hexval(char c) {
	if ('0' <= c && c <= '9') return c - '0';
	if ('a' <= c && c <= 'f') return c - 'a' + 10;
	if ('A' <= c && c <= 'F') return c - 'A' + 10;
	return -1;
}

static void feed_byte(struct http_client *client, struct http_response *resp, char byte) {
	if (save_byte[client->state]) {
		resp->buff[resp->len] = byte;
		resp->len++;
	}

	switch (client->state) {
	case STATUS_LINE:
		if (byte == '\r')
			client->state = STATUS_LINE_LF;
		else
			resp->status_len++;
		break;
	case STATUS_LINE_LF:
		client->state = HEADERS;
		break;
	case HEADERS:
		resp->header_len++;
		if (byte == '\r') client->state = HEADERS1;
		break;
	case HEADERS1:
		resp->header_len++;
		client->state = HEADERS2;
		break;
	case HEADERS2:
		if (byte == '\r') {
			client->state = HEADERS3;
		}
		else {
			client->state = HEADERS;
			resp->header_len++;
		}
		break;
	case HEADERS3:
		/* null-terminate the response for easier encoding recognition
		 * using the extra byte from realloc in http_read */
		resp->buff[resp->len] = 0;
		if (chunk_encoding(resp->buff)) {
			client->state = CHUNK_SIZE;
		}
		else if ((client->bytes = length_encoding(resp->buff)) >= 0) {
			resp->body_len = client->bytes;
			if (client->bytes == 0)
				client->state = DONE;
			else
				client->state = LENGTH;
		}
		else {
			client->state = CLOSE;
		}
		break;
	case CHUNK_SIZE:
		if (hexval(byte) != -1) {
			client->bytes *= 16;
			client->bytes += hexval(byte);
		}
		else {
			client->state = CHUNK_SIZE_LF;
		}
		break;
	case CHUNK_SIZE_LF:
		resp->body_len += client->bytes;
		if (client->bytes <= 0) {
			client->bytes = 2;
			client->state = CHUNK_LAST;
		}
		else {
			client->state = CHUNK;
		}
		break;
	case CHUNK:
		client->bytes--;
		if (client->bytes <= 0) {
			client->bytes = 2;
			client->state = CHUNK_CRLF;
		}
		break;
	case CHUNK_CRLF:
		client->bytes--;
		if (client->bytes <= 0) {
			client->bytes = 0;
			client->state = CHUNK_SIZE;
		}
		break;
	case CHUNK_LAST:
	case LENGTH:
		client->bytes--;
		if (client->bytes <= 0)
			client->state = DONE;
		break;
	case CLOSE:
		resp->body_len++;
		break;
	}
}

int http_recv_part(struct http_client *client, struct http_response *resp) {
	char buffer[READ_BUFF_SIZE];
	int nbytes;

	do {
		if (client->ssl) {
			nbytes = SSL_read(client->ssl, buffer, READ_BUFF_SIZE);
			if (nbytes <= 0) {
				if (SSL_get_error(client->ssl, nbytes) == SSL_ERROR_ZERO_RETURN)
					nbytes = 0;
				else
					nbytes = -1;
			}
		}
		else {
			nbytes = read(client->sockfd, buffer, READ_BUFF_SIZE);
		}

		if (nbytes < 0) {
			return -1;
		}
		else if (nbytes == 0) {
			if (client->state == CLOSE) {
				client->state = DONE;
				return 1;
			}
			else {
				return -1;
			}
		}

		char *new_resp = realloc(resp->buff, resp->len + nbytes + 1);
		if (!new_resp)
			return -1;

		resp->buff = new_resp;

		for (int i = 0; i < nbytes; i++)
			feed_byte(client, resp, buffer[i]);
	} while (client->ssl && SSL_pending(client->ssl));
	
	if (client->state == DONE) {
		client->state = STATUS_LINE;
		resp->headers = resp->buff + resp->status_len + 2; // 2 extra bytes from crlf
		resp->body = resp->headers + resp->header_len + 2; // same
		resp->buff[resp->len] = 0; // set the spare byte to 0 for convenience
		return 1;
	}

	return 0;
}

int http_recv(struct http_client *client, struct http_response *resp) {
	int res;

	memset(resp, 0, sizeof(struct http_response));
	do {
		res = http_recv_part(client, resp);
	} while (res == 0);

	return res;
}

static int intlen(int x) {
	int res = 0;

	while (x > 0) {
		res++;
		x /= 10;
	}

	return res;
}

int http_send(struct http_client *client, struct http_request *req) {
	char *text;
	char *method;
	int size = 0;
	int res;

	method = req->data_len ? "POST" : "GET";

	size += 12 + strlen(method) + strlen(req->path); // method path HTTP/1.1\r\n
	size += 8 + strlen(req->host); // Host: host\r\n
	if (req->agent)
		size += 14 + strlen(req->agent); // User-Agent: agent\r\n
	if (req->referer)
		size += 11 + strlen(req->referer); // Referer: referer\r\n
	if (req->cookie)
		size += 10 + strlen(req->cookie); // Cookie: cookie\r\n
	if (req->data_len > 0) {
		size += 18 + intlen(req->data_len); // Content-Length: length\r\n
		size += 49; // Content-Type: application/x-www-form-urlencoded\r\n
	}
	size += 2; // \r\n
	size += req->data_len; // data

	text = malloc(size + 1);
	if (!text) return -1;

	text += sprintf(text, "%s %s HTTP/1.1\r\n", method, req->path);
	text += sprintf(text, "Host: %s\r\n", req->host);
	if (req->agent)
		text += sprintf(text, "User-Agent: %s\r\n", req->agent);
	if (req->referer)
		text += sprintf(text, "Referer: %s\r\n", req->referer);
	if (req->cookie)
		text += sprintf(text, "Cookie: %s\r\n", req->cookie);
	if (req->data_len > 0) {
		text += sprintf(text, "Content-Length: %d\r\n", req->data_len);
		text += sprintf(text, "Content-Type: application/x-www-form-urlencoded\r\n");
	}
	text += sprintf(text, "\r\n");

	memcpy(text, req->data, req->data_len);
	text -= size - req->data_len;
	
	if (client->ssl)
		res = SSL_write(client->ssl, text, size);
	else
		res = write(client->sockfd, text, size);

	free(text);
	return res > 0 ? 0 : -1;
}
