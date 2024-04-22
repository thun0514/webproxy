#include <stdio.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

#define DEFAULT_PORT "80"
#define DEFAULT_PATH "/"
#define NEW_VERSION "HTTP/1.0"

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";

void doit(int fd);
int parse_uri(char *uri, char *hostname, char *port, char *path);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);

int main(int argc, char **argv) {
  int listenfd, connfd;                   // 소켓 디스크립터를 저장할 변수 선언
  char hostname[MAXLINE], port[MAXLINE];  // 호스트네임과 포트 정보를 저장할 변수 선언
  socklen_t clientlen;                    // 클라이언트의 주소 길이를 저장할 변수 선언
  struct sockaddr_storage clientaddr;     // 클라이언트의 소켓 주소 정보를 저장할 변수 선언

  /* Check command line args */
  if (argc != 2) {
    /* 명령행 인수가 2개가 아닌 경우 사용법 출력 후 종료 */
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  /* 클라이언트의 연결을 수신 대기하는 소켓 생성 */
  listenfd = Open_listenfd(argv[1]); // 주어진 포트로 소켓을 열고 듣기 상태로 설정

  while (1) {
    /* 클라이언트로부터의 연결을 수락하고 처리 */
    clientlen = sizeof(clientaddr); // 클라이언트의 주소 길이를 구함
    connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen); // 클라이언트의 연결을 수락하고 연결된 소켓 디스크립터를 반환
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0); // 클라이언트의 주소 정보를 호스트네임과 포트로 변환
    printf("Accepted connection from (%s, %s)\n", hostname, port); // 클라이언트의 연결을 콘솔에 출력
    doit(connfd);   // 클라이언트와의 연결을 처리하는 함수 호출
    Close(connfd);  // 클라이언트와의 연결을 끊음
  }
}

/* 클라이언트의 요청을 처리하는 함수 */
void doit(int fd) {
  int serverfd;
  char request_buf[MAXLINE], response_buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE], new_hdr[MAXLINE];
  char server_hostname[MAXLINE], server_port[MAXLINE], server_path[MAXLINE];
  rio_t rio;

  /* Read request line and headers */
  Rio_readinitb(&rio, fd);            // 클라이언트와의 연결을 읽기 위해 rio 구조체 초기화
  Rio_readlineb(&rio, request_buf, MAXLINE);  // 클라이언트로부터 요청 라인 읽기
  printf("Request header:\n");
  printf("%s", request_buf);
  sscanf(request_buf, "%s %s %s", method, uri, version);  // 요청 라인 파싱

  /* GET / HEAD Method가 아닐 때 */
  if (strcasecmp(method, "GET") && strcasecmp(method, "HEAD")) {
    /* 요청 메서드가 GET이 아닌 경우 "501 Not implemented" 오류 반환 */
    clienterror(fd, method, "501", "Not implemented", "Tiny does not implement this method");
    return;
  }

  if(strstr(uri, "favicon")) return;
  
  parse_uri(uri, server_hostname, server_port, server_path);

  sprintf(request_buf, "%s %s %s\r\n", method, server_path, NEW_VERSION);

  /* 클라이언트로부터 받은 요청을 서버로 전송 */
  serverfd = Open_clientfd(server_hostname, server_port);
  Rio_writen(serverfd, request_buf, strlen(request_buf));

  while (strcmp(request_buf, "\r\n")) {
    Rio_readlineb(&rio, request_buf, MAXLINE);
    Rio_writen(serverfd, request_buf, strlen(request_buf));
  }

  // 6.서버 응답이 오면 클라이언트에게 전달한다.
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
  /* URI에서 시작 위치 설정 */
  char *ptr = uri;

  ptr = strstr(ptr, "://");
  ptr = ptr ? ptr + 3 : uri;
  if (ptr[0] == '/') ptr += 1;

  /* Hostname 추출 */
  strcpy(hostname, ptr);

  /* Path 추출 */
  if((ptr = strchr(hostname, '/'))){  
    *ptr = '\0';                      // host = www.google.com:80
    ptr += 1;
    strcpy(path, "/");                // uri_ptos = /
    strcat(path, ptr);                // uri_ptos = /index.html
  } else {
    strcpy(path, "/");
  }

  /* Port Number 추출 */
  if ((ptr = strchr(hostname, ':'))){ // host = www.google.com:80
    *ptr = '\0';                      // host = www.google.com
    ptr += 1;     
    strcpy(port, ptr);                // port = 80
  } else {
    strcpy(port, "80");               // port가 없을 경우 "80"을 넣어줌
  }

  // printf("Host: %s\n", hostname);
  // printf("Port: %s\n", port);
  // printf("Path: %s\n", path);

  return 0; // 성공
}
