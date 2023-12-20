#include <errno.h>
#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include "defs.h"
#define YLD() do{}while(0)
#define RET { printf("%d Failed. Errno: %d k: %d\n", x, errno, __k); goto end; }
#include "afewmacros.h"

#define SA struct sockaddr

int cnt = 1;
int subcnt = 1;
const char* srv = "127.0.0.1";

void connection(const char *server, int x, int n) {
  int sockfd, connfd, id = 0;
  struct sockaddr_in servaddr, cli;
  char buf[128], ret[128];

  sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd == -1) {
    printf("%d socket creation failed...\n", x);
    return;
  }

  memset(&servaddr, 0, sizeof(servaddr));

  // assign IP, PORT
  servaddr.sin_family = AF_INET;
  servaddr.sin_addr.s_addr = inet_addr(server);
  servaddr.sin_port = htons(8001);

  if (connect(sockfd, (SA*)&servaddr, sizeof(servaddr)) != 0) {
    printf("%d connection with the server failed...\n", x);
    return;
  }
  int i = 0;
  for (; i < n; i++) {
    int l = snprintf(buf, 128, "%d-%d\r\n", x, i);
    WRITEN(sockfd, buf, l);
    READN(sockfd, ret, l);
    if (memcmp(buf, ret, l)) {
      buf[l] = 0;
      ret[l] = 0;
      printf("%d Error: \"%s\" vs \"%s\"", x, buf, ret);
      break;
    }
  }
end:
  if (i != n)
      printf("%d Failed\n", x);
    else
      printf("%d Success\n", x);

  close(sockfd);
}

void* worker(intptr_t i) {
  connection(srv, i, subcnt);
}

int main(int argc, char *argv[]) {

  // server ip
  if (argc > 1) {
    srv = argv[1];
  }

  // Num of threads / connections
  if (argc > 2) {
    cnt = atoi(argv[2]);
    if (cnt <= 0) cnt = 1;
  }

  // Num of requests per tgread
  if (argc > 3) {
    subcnt = atoi(argv[3]);
    if (subcnt <= 0) subcnt = 1;
  }

  pthread_t* tid = (pthread_t*)malloc(cnt * sizeof(pthread_t));

  for (intptr_t i = 1; i < cnt; i++) {
    if (pthread_create(tid + i, NULL, (void * (*)(void *))&worker, (void*)i)) {
      printf("T%lu pthread_create failed...\n", i);
      exit(EXIT_FAILURE);
    }
  }

  connection(srv, 0, subcnt);

  for (int i = 1; i < cnt; i++) {
    pthread_join(tid[i], NULL);
  }

  free(tid);

  return 0;
}
