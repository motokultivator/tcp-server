#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <netinet/in.h>

#include "defs.h"

#define RET return
#define YLD() do { task_yld(); if (c->flags & (EPOLLERR | EPOLLHUP)) RET; } while(0)

#include "afewmacros.h"

#define BUF_SIZE 1024

void process(const struct client_state* c) {
  char buf[BUF_SIZE];
  char* sspl;
  int l, k;
  while (1) {
    READSEP(l, sspl, c->fd, buf, BUF_SIZE -1, "\r\n", 2);
    if (l) {
      WRITEN(c->fd, buf, l);
    }
  }
}

int main() {
  struct serv_desc srv[] = {
    {
    .process = process,
    .port = 8001,
    .ip = INADDR_ANY,
    },
    {
      .process = NULL,
    }
  };

  server_run(srv);
}
