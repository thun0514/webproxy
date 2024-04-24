#include <stdio.h>
#include "csapp.h"
#include "cache.h"


#define DEFAULT_PORT "80"
#define DEFAULT_PATH "/"
#define NEW_VERSION "HTTP/1.0"

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";
static const char *endof_hdr = "\r\n";
static const char *host_hdr_format = "Host: %s\r\n";
static const char *conn_hdr = "Connection: close\r\n";
static const char *prox_hdr = "Proxy-Connection: close\r\n";

static const char *host_key = "Host";
static const char *connection_key = "Connection";
static const char *proxy_connection_key = "Proxy-Connection";
static const char *user_agent_key = "User-Agent";

/* For cache */
LRU_Cache *cache;

void doit(int fd);
int parse_uri(char *uri, char *hostname, char *port, char *path);
// void parse_uri(char *uri, char *hostname, char *path, int *port);
void build_http_header(char *http_header, char *hostname, char *path,
                        char* port, char *method, rio_t *rio);
void *thread (void *vargp);

int main(int argc, char **argv) {
  int listenfd, *connfdp;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;
  pthread_t tid;

  if (argc != 2) {
    /* 명령행 인수가 2개가 아닌 경우 사용법 출력 후 종료 */
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  /* 특정 클라이언트가 종료되었을 때 프로그램이 비정상적으로 종료되는 것을 무시 */
  Signal(SIGPIPE, SIG_IGN);

  cache = createCache(MAX_CACHE_SIZE);

  /* 클라이언트의 연결을 수신 대기하는 소켓 생성 */
  listenfd = Open_listenfd(argv[1]);

  /* 클라이언트로부터의 연결을 수락하고 처리 */
  while (1) {
    clientlen = sizeof(clientaddr);

    connfdp = Malloc(sizeof(int));
    *connfdp = Accept(listenfd, (SA *)&clientaddr, &clientlen);
    /* 클라이언트의 주소 정보를 호스트네임과 포트로 변환 */
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, 
                MAXLINE, port, MAXLINE, 0);
    printf("Accepted connection from (%s, %s)\n", hostname, port);

    // doit(*connfdp);
    // close(*connfdp);

    /* 스레드 생성하여 클라이언트 요청 처리 */
    Pthread_create(&tid, NULL, thread, connfdp);  
  }
  freeCache(cache);
  return 0;
}

void *thread (void *vargp) {
  int connfd = *((int *)vargp);
  Pthread_detach(pthread_self()); // 스레드 분리
  Free(vargp);                    // 동적 할당된 메모리 해제
  doit(connfd);                   // 클라이언트 요청 처리 함수 호출
  Close(connfd);                  // 클라이언트 소켓 닫기
  return NULL;
}

/* 클라이언트의 요청을 처리하는 함수 */
void doit(int fd) {
  int serverfd;
  char buf[MAXLINE],
      method[MAXLINE], uri[MAXLINE], version[MAXLINE], new_header[MAXLINE],
      server_hostname[MAXLINE], server_path[MAXLINE], server_port[MAXLINE];
  rio_t rio;

  /* 클라이언트로부터 요청 라인 및 헤더를 읽음 */
  Rio_readinitb(&rio, fd);  // 클라이언트와의 연결을 읽기 위해 rio 구조체 초기화
  Rio_readlineb(&rio, buf, MAXLINE);  // 클라이언트로부터 요청 라인 읽기
  printf("Request header:\n");
  printf("%s", buf);
  sscanf(buf, "%s %s %s", method, uri, version);  // 요청 라인 파싱

  /* 지원하지 않는 method인 경우 예외 처리 */
  if (strcasecmp(method, "GET") && strcasecmp(method, "HEAD")) {
    printf("Proxy does not implement the method");
    return;
  }

  if(strstr(uri, "favicon")) return; 
  
  parse_uri(uri, server_hostname, server_port, server_path);
  build_http_header(new_header, server_hostname, server_path, server_port, method, &rio);
  
  // 캐시 검사
  Node *cache_node = find_cache(cache, uri);
  if (cache_node) {             // 캐시 된 웹 객체가 있으면
    send_cache(fd, cache_node); // 캐싱된 웹 객체를 Client에 바로 전송
    moveToHead(cache, cache_node); // 사용한 웹 객체의 순서를 맨 앞으로 갱신 후 return
    return;
  }

  /* 클라이언트로부터 받은 요청을 서버로 전송 */
  serverfd = Open_clientfd(server_hostname, server_port);
  if (serverfd < 0) {
    printf("connection failed\n");
    return;
  }

  // write the http header to endserver
  Rio_writen(serverfd, new_header, strlen(new_header));

  // recieve message from end server and send to the client
  char cachebuf[MAX_OBJECT_SIZE];
  int sizebuf = 0;
  size_t n;
  while ((n=Rio_readn(serverfd, buf, MAXLINE)) != 0) {
    /* 서버에서 보낸 응답을 저장 후 클라이언트로 전송*/
    if (sizebuf < MAX_OBJECT_SIZE)
      memcpy(cachebuf + sizebuf, buf,n);
    sizebuf += n;
    Rio_writen(fd, buf, n);
  }
  if (sizebuf < MAX_OBJECT_SIZE) // 캐시 가능한 크기면 캐시 추가
    add_cache(cache, uri, cachebuf, sizebuf);

  Close(serverfd);
}

/* URI에서 hostname, port, path를 추출 */
int parse_uri(char *uri, char *hostname, char *port, char *path) {
  /* URI에서 시작 위치 설정 */
  char *ptr = strstr(uri, "://");
  ptr = ptr ? ptr + 3 : uri;
  if (ptr[0] == '/') ptr += 1;

  /* Hostname 추출 */
  strcpy(hostname, ptr);

  /* Path 추출 */
  if((ptr = strchr(hostname, '/'))){ 
    *ptr = '\0';                // host = www.google.com:80
    ptr += 1;
    strcpy(path, DEFAULT_PATH); // uri_ptos = /
    strcat(path, ptr);          // uri_ptos = /index.html
  } else {
    strcpy(path, DEFAULT_PATH);
  }
  /* Port Number 추출 */
  if ((ptr = strchr(hostname, ':'))){ // host = www.google.com:80
    *ptr = '\0';                      // host = www.google.com
    ptr += 1;     
    strcpy(port, ptr);                // port = 80
  } else {
    strcpy(port, DEFAULT_PORT);       // port가 없을 경우 "80"을 넣어줌
  }

  return 0; // 성공
}
/* 새로운 헤더 만드는 함수 */
void build_http_header(char *http_header, char *hostname, char *path, char *port, char *method, rio_t *client_rio) {
  char buf[MAXLINE], request_hdr[MAXLINE], other_hdr[MAXLINE], host_hdr[MAXLINE];
  
  // request line
  sprintf(request_hdr, "%s %s %s\r\n", method, path, NEW_VERSION);

  // get other request header for client rio and change it
  while (Rio_readlineb(client_rio, buf, MAXLINE) > 0) {
    if (strcmp(buf, endof_hdr) == 0)
      break;  // EOF
    
    if (!strncasecmp(buf, host_key, strlen(host_key))) {
      sprintf(host_hdr, "Host: %s\r\n", hostname);
      continue;
    }

    if (strncasecmp(buf, connection_key, strlen(connection_key))
      && strncasecmp(buf, proxy_connection_key, strlen(proxy_connection_key))
      && strncasecmp(buf, user_agent_key, strlen(user_agent_key))) {
      strcat(other_hdr, buf);
    }

  }
  if (strlen(host_hdr) == 0) {
    sprintf(host_hdr, host_hdr_format, hostname);
  }
  sprintf(http_header, "%s%s%s%s%s%s%s",
          request_hdr,
          host_hdr,
          user_agent_hdr,
          other_hdr,
          conn_hdr,
          prox_hdr,
          endof_hdr);
  return;
}