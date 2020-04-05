struct http_client {
	int sockfd;
	void *ssl;
	
	int bytes;
	int state;
};

struct http_request {
	char *host;
	char *path;

	char *agent;
	char *referer;
	char *cookie;

	char *data;
	int data_len;
};

struct http_response {
	char *buff;
	char *headers;
	char *body;

	int len;
	int status_len;
	int header_len;
	int body_len;
};

/*
Initializes the ssl library. Only needs to be done if you plan on using https.
*/
void http_init();

/*
Connects to a host.
 - host - hostname
 - secure - 0 is http, 1 is https
Returns:
 0 - success
-1 - fail
*/
int http_connect(struct http_client *client, const char *host, int secure);

/*
Disconects from the previously connected host.
*/
void http_disconnect(struct http_client *client);

/*
Makes a request to http(s)://<host><path>
 - req.host - <host>, eg. "example.com"
 - req.path - <path>, must start with a slash, eg. "/index.html", "/"
 - req.agent - user agent, may be null
 - req.referer - referer, may be null
 - req.cookie - cookies to send, may be null
 - req.data - data if making POST request, may be null
 - req.data_len - length of data, set to 0 for a GET request
Returns:
 0 - success
-1 - fail
*/
int http_send(struct http_client *client, struct http_request *req);

/*
Receives the entire previously made response.
 - resp.buff - buffer for the entire response, freeing is left to the user
 - resp.headers - pointer to start of headers
 - resp.body - pointer to start of body
 - resp.len - length of the entire response
 - resp.status_len - length of the status line (at the start of the response)
 - resp.header_len - length of headers
 - resp.body_len - length of body
Returns:
 1 - success (!! 1 instead of 0 for uniformity with http_recv_part)
-1 - fail
*/
int http_recv(struct http_client *client, struct http_response *resp);

/*
Receives the response piece by piece. You can use this with select(2) for
dealing with multiple transfers happening at once.
 - resp - same as above, MUST be zeroed out before the first call
Returns:
 1 - success, transfer finished
 0 - success, more data to come
-1 - fail
*/
int http_recv_part(struct http_client *client, struct http_response *resp);

/*
Uninitializes the ssl library. Use only if you called http_init
*/
void http_uninit();
