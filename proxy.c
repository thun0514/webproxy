#include <stdio.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

#define DEFAULT_PORT "80"
#define DEFAULT_PATH "/"
#define NEW_VERSION "HTTP/1.0"

#define CONCURRENCY     1 // 0: 시퀀셜, 1: 멀티스레드, 2: 멀티프로세스

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";

void doit(int fd);
int parse_uri(char *uri, char *hostname, char *port, char *path);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);
void new_requesthdrs(rio_t *rio, char *buf, char *hostname,
                      char *port, char *path, char *method);
#if CONCURRENCY == 1
void *thread (void *vargp);
#endif

int main(int argc, char **argv) {
  int listenfd, *connfdp;                 // 소켓 디스크립터를 저장할 변수 선언
  char hostname[MAXLINE], port[MAXLINE];  // 호스트네임과 포트 정보를 저장할 변수 선언
  socklen_t clientlen;                    // 클라이언트의 주소 길이를 저장할 변수 선언
  struct sockaddr_storage clientaddr;     // 클라이언트의 소켓 주소 정보를 저장할 변수 선언
  pthread_t tid;

  if (argc != 2) {
    /* 명령행 인수가 2개가 아닌 경우 사용법 출력 후 종료 */
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

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

  #if CONCURRENCY == 0
    doit(*connfdp);
    close(*connfdp);
  #elif CONCURRENCY == 1
    /* 스레드 생성하여 클라이언트 요청 처리 */
    Pthread_create(&tid, NULL, thread, connfdp);  
  #endif
  }
  return 0;
}

#if CONCURRENCY == 1
void *thread (void *vargp) {
  int connfd = *((int *)vargp);
  Pthread_detach(pthread_self()); // 스레드 분리
  Free(vargp);                    // 동적 할당된 메모리 해제
  doit(connfd);                   // 클라이언트 요청 처리 함수 호출
  Close(connfd);                  // 클라이언트 소켓 닫기
  return NULL;
}
#endif

/* 클라이언트의 요청을 처리하는 함수 */
void doit(int fd) {
  int serverfd;
  char request_buf[MAXLINE], response_buf[MAXLINE],
      method[MAXLINE], uri[MAXLINE], version[MAXLINE],
      server_hostname[MAXLINE], server_port[MAXLINE], server_path[MAXLINE];
  rio_t rio;

  /* 클라이언트로부터 요청 라인 및 헤더를 읽음 */
  Rio_readinitb(&rio, fd);  // 클라이언트와의 연결을 읽기 위해 rio 구조체 초기화
  Rio_readlineb(&rio, request_buf, MAXLINE);  // 클라이언트로부터 요청 라인 읽기
  printf("Request header:\n");
  printf("%s", request_buf);
  sscanf(request_buf, "%s %s %s", method, uri, version);  // 요청 라인 파싱

  /* GET / HEAD Method가 아닐 때 */
  if (strcasecmp(method, "GET") && strcasecmp(method, "HEAD")) {
    clienterror(fd, method, "501", "Not implemented",
                "Tiny does not implement this method");
    return;
  }

  if(strstr(uri, "favicon")) return;  // 파비콘 요청이 있는 경우 처리하지 않음

  parse_uri(uri, server_hostname, server_port, server_path);
  new_requesthdrs(&rio, request_buf, server_hostname, 
                  server_port, server_path, method);
  
  /* 클라이언트로부터 받은 요청을 서버로 전송 */
  serverfd = Open_clientfd(server_hostname, server_port);
  if (serverfd < 0) {
    clienterror(serverfd, "Connection failed", "500",
                "Internal Server Error", "Failed to connect to server");
    return;
  }

  /* 서버로 요청 전송 */
  Rio_writen(serverfd, request_buf, strlen(request_buf));

  /* 요청 헤더를 서버로 전송 */
  while (strcmp(request_buf, "\r\n")) {
    Rio_readlineb(&rio, request_buf, MAXLINE);
    Rio_writen(serverfd, request_buf, strlen(request_buf));
  }

  /* 서버로부터 응답을 읽고 클라이언트에게 전송 */
  while (Rio_readn(serverfd, response_buf, MAXLINE) > 0) {
    Rio_writen(fd, response_buf, MAXLINE);
  }

  Close(serverfd);
}

/* 클라이언트에게 오류를 설명하는 HTML 문서를 전송하여 오류를 처리하는 함수 */
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg) {
  char request_buf[MAXLINE], body[MAXBUF];

  /* Build the HTTP response body */
  sprintf(body, "<html><title>Tiny Error</title>");
  sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
  sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
  sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
  sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

  /* Print the HTTP response */
  sprintf(request_buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
  Rio_writen(fd, request_buf, strlen(request_buf));
  sprintf(request_buf, "Content-type: text/html\r\n");
  Rio_writen(fd, request_buf, strlen(request_buf));
  sprintf(request_buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
  Rio_writen(fd, request_buf, strlen(request_buf));
  Rio_writen(fd, body, strlen(body));
}

int parse_uri(char *uri, char *hostname, char *port, char *path) {
  char *ptr = uri;

  /* URI에서 시작 위치 설정 */
  ptr = strstr(ptr, "://");
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

  // printf("Host: %s\n", hostname);
  // printf("Port: %s\n", port);
  // printf("Path: %s\n", path);

  return 0; // 성공
}
/* 새로운 요청 헤더를 생성하는 함수 */
void new_requesthdrs(rio_t *rio, char *buf, char *hostname, char *port,
                      char *path, char *method) {
  sprintf(buf, "%s %s %s\r\n", method, path, NEW_VERSION);
  sprintf(buf, "%sHost: %s\r\n", buf, hostname);          // Host: www.google.com     
  sprintf(buf, "%s%s", buf, user_agent_hdr);              // User-Agent: ~(bla bla)
  sprintf(buf, "%sConnections: close\r\n", buf);          // Connections: close
  sprintf(buf, "%sProxy-Connection: close\r\n\r\n", buf); // Proxy-Connection: close

  // char host_header[MAXLINE], other_header[MAXLINE];
  // strcpy(host_header, "\0");
  
  
  // /*check host header and get other request header for client rio then change it */
  // while(Rio_readlineb(rio, buf, MAXLINE) >0){
  //   if(strcmp(buf, "\r\n")==0) break; //EOF
  //   if(strncasecmp("Host:", buf, strlen("Host:"))){
  //     strcpy(host_header, buf);
  //     continue;
  //   }
  //   //when this line is not  proxy_connection header or connection header or user-agent:
  //   if( !strncasecmp("Connection:", buf, strlen("Connection:")) 
  //     && !strncasecmp("Proxy-Connection:", buf, strlen("Proxy-Connection:"))
  //     && !strncasecmp("User-Agent:", buf, strlen("User-Agent:")) )
  //   {
  //     strcat(other_header, buf);
  //   }
  // }
  // if(strlen(host_header) == 0){
  //   sprintf(host_header, "Host: %s：%s\r\n", hostname, port);
  // }
  // strcat(buf, host_header);
  // strcat(buf, user_agent_hdr);
  // strcat(buf, other_header);
  // strcat(buf, "Connection: close\r\n");
  // strcat(buf, "Proxy-Connection: close\r\n");
  // strcat(buf, "\r\n");

  
  // printf("%s\n", buf);
}

