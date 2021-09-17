/*
 * proxy.c - A Basic Web proxy
 *
 * @author: Jaidev Singh Saberwal
 * 
 * IMPORTANT: Give a high level description of your code here. You
 * must also provide a header comment at the beginning of each
 * function that describes what that function does.
 * 
 * This is essentially a concurrent proxy through the 
 * usage of threads. I used the same given proxy and 
 * edited the file so that it could handle every client with
 * threads.
 */

#include "csapp.h"

/* The name of the proxy's log file */
#define PROXY_LOG "proxy.log"

/* Undefine this if you don't want debugging output */
#define DEBUG

/*
 * Globals
 */ 
FILE *log_file; /* Log file with one line per HTTP request */
sem_t mutex1;   /* mutex lock */
sem_t mutex2;

/*
 * Functions not provided to the students
 */
int open_clientfd(char *hostname, int port); 
ssize_t Rio_readn_w(int fd, void *ptr, size_t nbytes);
ssize_t Rio_readlineb_w(rio_t *rp, void *usrbuf, size_t maxlen); 
void Rio_writen_w(int fd, void *usrbuf, size_t n);

/*
 * Function prototypes
 */
int parse_uri(char *uri, char *target_addr, char *path, int  *port);
void format_log_entry(char *logstring, struct sockaddr_in *sockaddr, char *uri, int size);
void client(int connfd);
void server(char *hostname, char *pathname, int serverport, char *rest_of_request, rio_t rio,char *buf, int connfd, char *request_uri);
/* 
 * main - Main routine for the proxy program 
 */
 
 
void threadproxy(int connfd){
	char *request;                  /* HTTP request from client */
	char *request_uri;              /* Start of URI in first HTTP request header line */
	char *request_uri_end;          /* End of URI in first HTTP request header line */
	char *rest_of_request;          /* Beginning of second HTTP request header line */
	int request_len;                /* Total size of HTTP request */	
	int i, n;                       /* General index and counting variables */
	int realloc_factor;             /* Used to increase size of request buffer if necessary */  

	char hostname[MAXLINE];         /* Hostname extracted from request URI */
	char pathname[MAXLINE];         /* Content pathname extracted from request URI */
	int serverport;                 /* Port number extracted from request URI (default 80) */
	

	rio_t rio;                      /* Rio buffer for calls to buffered rio_readlineb routine */
	char buf[MAXLINE];              /* General I/O buffer */
	
	int serverfd;                   /* Socket descriptor for talking with end server */
	char log_entry[MAXLINE];        /* Formatted log entry */
	int response_len;               /* Total size in bytes of response from end server */
    	struct sockaddr_in clientaddr;  /* Clinet address structure*/  
    	
	/* 
	* Read the entire HTTP request into the request buffer, one line
	* at a time.
	*/
	request = (char *)Malloc(MAXLINE);
	request[0] = '\0';
	realloc_factor = 2;
	request_len = 0;
	Rio_readinitb(&rio, connfd);
	while (1) {
		if ((n = Rio_readlineb_w(&rio, buf, MAXLINE)) <= 0) {
		    printf("process_request: client issued a bad request (1).\n");
		    close(connfd);
		    free(request);
		    break;
		}

		/* If not enough room in request buffer, make more room */
		if (request_len + n + 1 > MAXLINE)
		    Realloc(request, MAXLINE*realloc_factor++);

		strcat(request, buf);
		request_len += n;

		/* An HTTP requests is always terminated by a blank line */
		if (strcmp(buf, "\r\n") == 0)
		    break;
    }
    

    /* 
     * Make sure that this is indeed a GET request
     */
    if (strncmp(request, "GET ", strlen("GET "))) {
	printf("process_request: Received non-GET request\n");
	close(connfd);
	free(request);
	return;
    }
    request_uri = request + 4;

    /* 
     * Extract the URI from the request
     */
    request_uri_end = NULL;
    for (i = 0; i < request_len; i++) {
	if (request_uri[i] == ' ') {
	    request_uri[i] = '\0';
	    request_uri_end = &request_uri[i];
	    break;
	}
    }

    /* 
     * If we hit the end of the request without encountering a
     * terminating blank, then there is something screwy with the
     * request
     */
    if ( i == request_len ) {
	printf("process_request: Couldn't find the end of the URI\n");
	close(connfd);
	free(request);
	return;
    }

    /*
     * Make sure that the HTTP version field follows the URI 
     */
    if (strncmp(request_uri_end + 1, "HTTP/1.0\r\n", strlen("HTTP/1.0\r\n")) &&
	strncmp(request_uri_end + 1, "HTTP/1.1\r\n", strlen("HTTP/1.1\r\n"))) {
	printf("process_request: client issued a bad request (4).\n");
	close(connfd);
	free(request);	
        return;
    }

    /*
     * We'll be forwarding the remaining lines in the request
     * to the end server without modification
     */
    rest_of_request = request_uri_end + strlen("HTTP/1.0\r\n") + 1;

    /* 
     * Parse the URI into its hostname, pathname, and port components.
     * Since the recipient is a proxy, the browser will always send
     * a URI consisting of a full URL "http://hostname:port/pathname"
     */  
    if (parse_uri(request_uri, hostname, pathname, &serverport) < 0) {
	printf("process_request: cannot parse uri\n");
	close(connfd);
	free(request);
	return;
    }    

	/*
	* Forward the request to the end server
	*/ 
	/*
	* open_clientfd uses gethostbyname
	*/
	P(&mutex2);
	if ((serverfd = open_clientfd(hostname, serverport)) < 0) {
		printf("process_request: Unable to connect to end server.\n");
		return;
	}
	V(&mutex2);
	
	Rio_writen_w(serverfd, "GET /", strlen("GET /"));
	Rio_writen_w(serverfd, pathname, strlen(pathname));
	Rio_writen_w(serverfd, " HTTP/1.0\r\n", strlen(" HTTP/1.0\r\n"));
	Rio_writen_w(serverfd, rest_of_request, strlen(rest_of_request));


	/*
	* Receive reply from server and forward on to client
	*/
	Rio_readinitb(&rio, serverfd);
	response_len = 0;
	while( (n = Rio_readn_w(serverfd, buf, MAXLINE)) > 0 ) {
		response_len += n;
		Rio_writen_w(connfd, buf, n);
		#if defined(DEBUG)	
		printf("Forwarded %d bytes from end server to client\n", n);
		fflush(stdout);
		#endif
		bzero(buf, MAXLINE);
	}

	/*
	* File lock, then Log the request to disk
	*/
	P(&mutex1);
	format_log_entry(log_entry, &clientaddr, request_uri, response_len);  
	fprintf(log_file, "%s %d\n", log_entry, response_len);
	fflush(log_file);
	V(&mutex1);

	/* Clean up to avoid memory leaks and then return */
	close(connfd);
	close(serverfd);


}

//this is the thread function that forwards to threadproxy method
void *thread(void *arg) {
    long mainthread = Pthread_self();
    int tid = (int*)arg;
    printf("Thread started\n", mainthread);
    threadproxy(tid);    
    
    Pthread_detach(Pthread_self());
    printf("Thread ended\n", mainthread);
    Pthread_exit(NULL);
    return NULL;
}

 
int main(int argc, char **argv)
{
    int listenfd;             /* The proxy's listening descriptor */
    int port;                 /* The port the proxy is listening on */
    int clientlen;            /* Size in bytes of the client socket address */
    struct sockaddr_in clientaddr;  /* Clinet address structure*/  
    int connfd;                     /* socket desciptor for talkign wiht client*/ 

    //create pthread
    pthread_t tid;      
    

    /* Check arguments */
    if (argc != 2) {
	fprintf(stderr, "Usage: %s <port number>\n", argv[0]);
	exit(0);
    }
    
    
    /* 
     * Ignore any SIGPIPE signals elicited by writing to a connection
     * that has already been closed by the peer process.
     */
    signal(SIGPIPE, SIG_IGN);

    //create a listening descriptor
    port = atoi(argv[1]);
    listenfd = Open_listenfd(port);

    //put into mutex
    log_file = Fopen(PROXY_LOG, "a");
    Sem_init(&mutex1, 0 ,1);
    Sem_init(&mutex2, 0 ,1);
    
    //we need to create the thread everytime there is a client
    while (1) { 
	clientlen = sizeof(clientaddr);  
	connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
	Pthread_create(&tid, NULL, thread, connfd);
    }

    /* Control never reaches here */
    exit(0);
}


/*
 * Rio_readn_w - A wrapper function for rio_readn (csapp.c) that
 * prints a warning message when a read fails instead of terminating
 * the process.
 */
ssize_t Rio_readn_w(int fd, void *ptr, size_t nbytes) 
{
    ssize_t n;
  
    if ((n = rio_readn(fd, ptr, nbytes)) < 0) {
	printf("Warning: rio_readn failed\n");
	return 0;
    }    
    return n;
}

/*
 * Rio_readlineb_w - A wrapper for rio_readlineb (csapp.c) that
 * prints a warning when a read fails instead of terminating 
 * the process.
 */
ssize_t Rio_readlineb_w(rio_t *rp, void *usrbuf, size_t maxlen) 
{
    ssize_t rc;

    if ((rc = rio_readlineb(rp, usrbuf, maxlen)) < 0) {
	printf("Warning: rio_readlineb failed\n");
	return 0;
    }
    return rc;
} 

/*
 * Rio_writen_w - A wrapper function for rio_writen (csapp.c) that
 * prints a warning when a write fails, instead of terminating the
 * process.
 */
void Rio_writen_w(int fd, void *usrbuf, size_t n) 
{
    if (rio_writen(fd, usrbuf, n) != n) {
	printf("Warning: rio_writen failed.\n");
    }	   
}

/*
 * parse_uri - URI parser
 * 
 * Given a URI from an HTTP proxy GET request (i.e., a URL), extract
 * the host name, path name, and port.  The memory for hostname and
 * pathname must already be allocated and should be at least MAXLINE
 * bytes. Return -1 if there are any problems.
 */
int parse_uri(char *uri, char *hostname, char *pathname, int *port)
{
    char *hostbegin;
    char *hostend;
    char *pathbegin;
    int len;

    if (strncasecmp(uri, "http://", 7) != 0) {
	hostname[0] = '\0';
	return -1;
    }
       
    /* Extract the host name */
    hostbegin = uri + 7;
    hostend = strpbrk(hostbegin, " :/\r\n\0");
    len = hostend - hostbegin;
    strncpy(hostname, hostbegin, len);
    hostname[len] = '\0';
    
    /* Extract the port number */
    *port = 80; /* default */
    if (*hostend == ':')   
	*port = atoi(hostend + 1);
    
    /* Extract the path */
    pathbegin = strchr(hostbegin, '/');
    if (pathbegin == NULL) {
	pathname[0] = '\0';
    }
    else {
	pathbegin++;	
	strcpy(pathname, pathbegin);
    }

    return 0;
}

/*
 * format_log_entry - Create a formatted log entry in logstring. 
 * 
 * The inputs are the socket address of the requesting client
 * (sockaddr), the URI from the request (uri), and the size in bytes
 * of the response from the server (size).
 */
void format_log_entry(char *logstring, struct sockaddr_in *sockaddr, 
		      char *uri, int size)
{
    time_t now;
    char time_str[MAXLINE];
    unsigned long host;
    unsigned char a, b, c, d;

    /* Get a formatted time string */
    now = time(NULL);
    strftime(time_str, MAXLINE, "%a %d %b %Y %H:%M:%S %Z", localtime(&now));

    /* 
     * Convert the IP address in network byte order to dotted decimal
     * form. Note that we could have used inet_ntoa, but chose not to
     * because inet_ntoa is a Class 3 thread unsafe function that
     * returns a pointer to a static variable (Ch 13, CS:APP).
     */
    host = ntohl(sockaddr->sin_addr.s_addr);
    a = host >> 24;
    b = (host >> 16) & 0xff;
    c = (host >> 8) & 0xff;
    d = host & 0xff;


    /* Return the formatted log entry string */
    sprintf(logstring, "%s: %d.%d.%d.%d %s", time_str, a, b, c, d, uri);
}