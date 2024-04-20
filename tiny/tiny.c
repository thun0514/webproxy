/* $begin tinymain */
/*
 * tiny.c - A simple, iterative HTTP/1.0 Web server that uses the
 *     GET method to serve static and dynamic content.
 *
 * Updated 11/2019 droh
 *   - Fixed sprintf() aliasing issue in serve_static(), and clienterror().
 */
#include "csapp.h"

void doit(int fd);
void read_requesthdrs(rio_t *rp);
int parse_uri(char *uri, char *filename, char *cgiargs);
void serve_static(int fd, char *filename, int filesize, char *method);
void get_filetype(char *filename, char *filetype);
void serve_dynamic(int fd, char *filename, char *cgiargs, char *method);
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
  int is_static;
  struct stat sbuf;
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char filename[MAXLINE], cgiargs[MAXLINE];
  rio_t rio;

  /* Read request line and headers */
  Rio_readinitb(&rio, fd);            // 클라이언트와의 연결을 읽기 위해 rio 구조체 초기화
  Rio_readlineb(&rio, buf, MAXLINE);  // 클라이언트로부터 요청 라인 읽기
  printf("Request header:\n");
  printf("%s", buf);
  sscanf(buf, "%s %s %s", method, uri, version);  // 요청 라인 파싱

  /* GET Method가 아닐 때 */
  if (!(strcasecmp(method, "GET") == 0 || strcasecmp(method, "HEAD") == 0)) {
    /* 요청 메서드가 GET이 아닌 경우 "501 Not implemented" 오류 반환 */
    clienterror(fd, method, "501", "Not implemented", "Tiny does not implement this method");
    return;
  }

  /* 요청 헤더 처리 */
  read_requesthdrs(&rio);

  /* URI 파싱 */
  is_static = parse_uri(uri, filename, cgiargs);  // URI를 파싱하여 정적인지 동적인지 확인

  /* 요청된 파일이 존재하는지 확인 */
  if (stat(filename, &sbuf) < 0) {
    /* 요청된 파일이 존재하지 않는 경우 "404 Not found" 오류 반환 */
    clienterror(fd, filename, "404", "Not found", "Tiny couldn't find this file");
    return;
  }

  /* 요청된 파일의 형태 확인 */
  if (is_static) {
    /* 정적 콘텐츠인 경우 권한 확인 */
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)) {
      /* 파일에 대한 읽기 권한이 없는 경우 "403 Forbidden" 오류 반환 */
      clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't read the file");
      return;
    }
    /* 정적 콘텐츠 제공 */
    serve_static(fd, filename, sbuf.st_size, method);

  } else {
    /* 동적 콘텐츠인 경우 권한 확인 */
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode)) {
      /* 파일에 대한 실행 권한이 없는 경우 "403 Forbidden" 오류 반환 */
      clienterror(fd, filename, "403", "Forbidden", "Tiny coudln't run the CGI program");
      return;
    }
    /* CGI 프로그램을 실행하여 동적 콘텐츠 생성 및 전송 */
    serve_dynamic(fd, filename, cgiargs, method);
  }
  
}

/* 클라이언트에게 오류를 설명하는 HTML 문서를 전송하여 오류를 처리하는 함수 */
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg) {
  char buf[MAXLINE], body[MAXBUF];

  /* Build the HTTP response body */
  sprintf(body, "<html><title>Tiny Error</title>");
  sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
  sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
  sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
  sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

  /* Print the HTTP response */
  sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-type: text/html\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
  Rio_writen(fd, buf, strlen(buf));
  Rio_writen(fd, body, strlen(body));
}

/* 요청 헤더를 읽어오는 함수 */
void read_requesthdrs(rio_t *rp) {
  char buf[MAXLINE];

  Rio_readlineb(rp, buf, MAXLINE);
  while (strcmp(buf, "\r\n")) {
    Rio_readlineb(rp, buf, MAXLINE);
    printf("%s", buf);
  }
  return;
}

int parse_uri(char *uri, char *filename, char *cgiargs) {
  char *ptr;

  if (!strstr(uri, "cgi-bin")) { /* Static content */
    strcpy(cgiargs, "");  // CGI 인수를 빈 문자열로 설정
    strcpy(filename, ".");  // 파일명을 현재 디렉토리로 설정
    strcat(filename, uri);  // URI를 파일명에 추가
    if (uri[strlen(uri)-1] == '/')  // URI가 '/'로 끝나는 경우
      strcat(filename, "home.html");  // "home.html" 파일명을 추가
    return 1;  // 정적 콘텐츠를 나타내는 값 1 반환
  } else { /* Dynamic content */
    ptr = index(uri, '?');  // URI에서 '?' 문자를 찾음
    if (ptr) {  // '?' 문자를 찾은 경우
      strcpy(cgiargs, ptr+1);  // CGI 인수를 '?' 문자 이후의 문자열로 설정
      *ptr = '\0';  // URI에서 '?' 문자 이후의 문자열을 제거하여 파일명으로 설정
    } else {  // '?' 문자를 찾지 못한 경우
      strcpy(cgiargs, "");  // CGI 인수를 빈 문자열로 설정
    }
    strcpy(filename, ".");  // 파일명을 현재 디렉토리로 설정
    strcat(filename, uri);  // URI를 파일명에 추가
    return 0;  // 동적 콘텐츠를 나타내는 값 0 반환
  }
}

void serve_static(int fd, char *filename, int filesize, char *method) {
  int srcfd;
  char *srcp, filetype[MAXLINE], buf[MAXBUF];

  /* Send response headers to client */
  get_filetype(filename, filetype);
  sprintf(buf, "HTTP/1.0 200 OK\r\n");  // HTTP 응답 헤더 생성
  sprintf(buf, "%sServer: Tiny Web Server\r\n", buf); // 서버 정보 추가
  sprintf(buf, "%sConnection: close\r\n", buf); // 연결 종료 설정
  sprintf(buf, "%sContent-length: %d\r\n", buf, filesize); // 콘텐츠 길이 설정
  sprintf(buf, "%sContent-type: %s\r\n\r\n", buf, filetype); // MIME 타입 설정
  Rio_writen(fd, buf, strlen(buf)); // 클라이언트에게 헤더 전송

  if (strcasecmp(method, "HEAD") == 0) { // HEAD 메소드를 요청 받았을 때
    return; // 응답 바디를 전송하지 않음
  }

  printf("Response header:\n");
  printf("%s", buf);

  /* Send response body to client */
  srcfd = Open(filename, O_RDONLY, 0); // 파일 열기
  // srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0); // 파일 메모리 매핑
  srcp = (char *)malloc(filesize);
  Rio_readn(srcfd ,srcp, filesize);
  Close(srcfd); // 파일 닫기
  Rio_writen(fd, srcp, filesize); // 클라이언트에게 파일 내용 전송
  // Munmap(srcp, filesize); // 메모리 매핑 해제
  free(srcp);
}

/*
 * get_filetype - Derive file type from filename
 */
void get_filetype(char *filename, char *filetype) {
  if (strstr(filename, ".html"))
    strcpy(filetype, "text/html");
  else if (strstr(filename, ".gif"))
    strcpy(filetype, "image/gif");
  else if (strstr(filename, ".png"))
    strcpy(filetype, "image/png");
  else if (strstr(filename, ".jpg"))
    strcpy(filetype, "image/jpeg");
  else if (strstr(filename, ".mp4"))
    strcpy(filetype, "video/mp4");
  else 
    strcpy(filetype, "text/plain");
}

void serve_dynamic(int fd, char *filename, char *cgiargs, char *method) {
  char buf[MAXLINE], *emptylist[] = { NULL };

  /* Return first part of HTTP response */
  sprintf(buf, "HTTP/1.0 200 OK\r\n"); // HTTP 응답 헤더 생성
  Rio_writen(fd, buf, strlen(buf)); // 클라이언트에게 헤더 전송
  sprintf(buf, "Server: Tiny Web Server\r\n"); // 서버 정보 추가
  Rio_writen(fd, buf, strlen(buf)); // 클라이언트에게 헤더 전송

  if (Fork() == 0) { /* Child */ // 자식 프로세스 생성
    /* Real server would set all CGI vars here */
    setenv("QUERY_STRING", cgiargs, 1); // QUERY_STRING 환경 변수를 URI에서 추출한 CGI 인수로 설정
    setenv("REQUEST_METHOD", method, 1); // REQUEST_METHOD 환경 변수를 URI에서 추출한 CGI 인수로 설정
    Dup2(fd, STDOUT_FILENO); // 표준 출력을 클라이언트에 연결
    Execve(filename, emptylist, environ); // CGI 프로그램 실행
  }
  Wait(NULL); // 부모 프로세스가 자식 프로세스의 종료를 대기
}