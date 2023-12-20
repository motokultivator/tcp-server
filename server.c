#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <assert.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>

#include "defs.h"

#define SA struct sockaddr

struct tinfo ti[NUM_THREADS];
unsigned long page_size;
unsigned long page_mask;

// ASM routines

// Initialize a new context on the given stack.
// Return pointer to context inside the stack.
void* init_ctx(void* stack, void* entry, entry_ptr proc, struct tinfo* ti, uint32_t j);
void swap_ctx(void* ctx);

static inline int _semiatomic_pop_slot(struct tinfo* t) {
  int lsp, ret;
  do {
    lsp = t->sp;
    if (lsp <= 0)
      return -1;
    ret = t->slots[lsp - 1];
  } while (!__sync_bool_compare_and_swap(&t->sp, lsp, lsp - 1));
  return ret;
}

static inline void _semiatomic_push_slot(struct tinfo* t, int j) {
  int lsp;
  do {
    lsp = t->sp;
    if (lsp >= CLIENTS_PER_THREAD) {
      W("%d You need some sleep...\n", t->id);
      exit(EXIT_FAILURE);
    }
    t->slots[lsp] = j;
  } while (!__sync_bool_compare_and_swap(&t->sp, lsp, lsp + 1));
}

static inline void _task_end(struct tinfo* ti, uint32_t j) {
  I("T%u:%u End. Closing fd: %d\n", ti->id, j, ti->cs[j].fd);

  if (epoll_ctl(ti->epoll_fd, EPOLL_CTL_DEL, ti->cs[j].fd, NULL) == -1) {
    W("T%u:%u epoll_ctl: DEL fd %d failed\n", ti->id, j, ti->cs[j].fd);
  }

  close(ti->cs[j].fd);
  ti->cs[j]._in_use = 0;
  swap_ctx(ti->ctx[j]);
}

static void task_wrap(entry_ptr entry, struct tinfo* ti, uint32_t j) {
  entry(ti->cs + j);
  _task_end(ti, j);
}

// TLS
__thread uint32_t current_client;
__thread struct tinfo* self;

void task_yld() {
  swap_ctx(self->ctx[current_client]);
}

void task_end() { // Experimental
  _task_end(self, current_client);
}

static void* worker(struct tinfo *ti) {
  uint32_t j, n;

  self = ti;

  stack_t ss, ss_old;
  // A stack in the stack.
  ss.ss_sp = alloca(SIGSTKSZ);

  if (ss.ss_sp == NULL) {
    W("T%u alloca failed\n", ti->id);
    exit(EXIT_FAILURE);
  }

  ss.ss_size = SIGSTKSZ;
  ss.ss_flags = 0;

  if (sigaltstack(&ss, &ss_old) == -1) {
    W("T%u sigaltstack failed\n", ti->id);
    exit(EXIT_FAILURE);
  }

  while (ti->enabled) {
    I("T%u polling\n", ti->id);
    int nfds = epoll_wait(ti->epoll_fd, ti->events, CLIENTS_PER_THREAD, POLL_TIMEOUT);

    if (nfds == -1) {
      W("T%u epoll_wait error %d\n", ti->id, errno);
      if (errno == EINTR)
        continue;
      exit(EXIT_FAILURE);
    }

    for (n = 0; n < nfds; ++n) {
      current_client = *((uint32_t*)ti->events[n].data.ptr); // Note: This is the exclusive place where current_client is being set.
      ti->cs[current_client].last_epoll_fd = *((int*)(((uint32_t*)ti->events[n].data.ptr) + 1)); // better
      ti->cs[current_client].flags = ti->events[n].events;
      I("T%u Switcing to %u...\n", ti->id, current_client);
      swap_ctx(ti->ctx[current_client]);
      if (!ti->cs[current_client]._in_use) {
        _semiatomic_push_slot(ti, current_client);
      }
      I("T%u Came back from %u!\n", ti->id, current_client);
    }
  }
  // TODO: Add watchdog or connection timeot.
  I("T%u is going to die\n", ti->id);

  for (j = 0; j < CLIENTS_PER_THREAD; j++) {
    if (ti->cs[j]._in_use) {
      I("T%u:%u Closing fd: %d\n", ti->id, j, ti->cs[j].fd);
      close(ti->cs[j].fd);
    }
  }
  sigaltstack(&ss_old, NULL); // Is it sensible?
  I("T%u ends\n", ti->id);
}

static void segfault_sigaction(int signal, siginfo_t *si, void *arg) {
  // Note: Here we have only SIGSTKSZ (usually 8192) bytes of a stack and have no overflow protection.
  // Anyway, it is far enough to call mprotect().
  // I("T%u:%u Segfault %p\n", self->id, current_client, si->si_addr);
  if (si->si_addr >= self->stack[current_client] && si->si_addr < self->stack[current_client] + (MAX_STACK_SIZE & page_mask)) {
    // This is access to stack. Extend the stack.
    if (mprotect((void*)((unsigned long)si->si_addr & page_mask), page_size, PROT_WRITE | PROT_READ)) {
      W("T%u:%u mprotect failed errno %d\n", self->id, current_client, errno);
      exit(EXIT_FAILURE);
    }
  } else {
    // This is a real segfault.
    W("T%u:%u Real fault addr: %p\n", self->id, current_client, si->si_addr);
    exit(EXIT_FAILURE);
  }
}

static void sigint_sigaction(int sig) {
  W("\nShutting down the server...\n");
}

void server_run(struct serv_desc* desc_array_zero_terminated) {
  int num_descs;
  int epoll_fd, connfd; // sockets
  int i, j, k;
  struct sockaddr_in addr;
  socklen_t len;
  struct sigaction sigact;
  struct epoll_event ev;
  struct serv_desc* desc;

  page_size = sysconf(_SC_PAGESIZE);
  page_mask = ~(page_size - 1);
  uint64_t stacks_map_size = CLIENTS_PER_THREAD * ((RED_ZONE_STACK_SIZE & page_mask) + (MAX_STACK_SIZE & page_mask));
  intptr_t stack_space = (MAX_STACK_SIZE & page_mask) + (RED_ZONE_STACK_SIZE & page_mask);

  memset(ti, 0, NUM_THREADS * sizeof(struct tinfo));

  memset(&sigact, 0, sizeof(struct sigaction));
  sigemptyset(&sigact.sa_mask);
  sigact.sa_flags = SA_NODEFER | SA_RESETHAND;
  sigact.sa_handler = sigint_sigaction;
  sigaction(SIGINT, &sigact, NULL);

  memset(&sigact, 0, sizeof(struct sigaction));
  sigemptyset(&sigact.sa_mask);
  sigact.sa_sigaction = segfault_sigaction;
  sigact.sa_flags = SA_SIGINFO | SA_ONSTACK;
  sigaction(SIGSEGV, &sigact, NULL);

  for (i = 0; i < NUM_THREADS; i++) {
    ti[i].epoll_fd = epoll_create1(0);
    I("T%u poll fd: %d\n", i, ti[i].epoll_fd);

    if (ti[i].epoll_fd == -1) {
      W("T%u epoll_create1 failed...\n", i);
      exit(EXIT_FAILURE);
    }

    ti[i].stack_address_space = mmap(NULL, stacks_map_size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
    if (ti[i].stack_address_space == NULL) {
      W("T%u mmap failed...\n", i);
      exit(EXIT_FAILURE);
    }

    for (j = 0; j < CLIENTS_PER_THREAD; j++) {
      ti[i].stack[j] = ti[i].stack_address_space + stack_space * j + (RED_ZONE_STACK_SIZE & page_mask);
      mprotect(ti[i].stack[j] + (MAX_STACK_SIZE & page_mask) - (MIN_STACK_SIZE & page_mask), MIN_STACK_SIZE & page_mask,
               PROT_WRITE | PROT_READ);
      ti[i].slots[j] = j;
      ti[i].cs[j]._in_use = 0;
      ti[i].cs[j].id = j;
    }

    ti[i].sp = CLIENTS_PER_THREAD;

    ti[i].enabled = 1;
    ti[i].id = i;

    if (pthread_create(&ti[i].tid, NULL, (void * (*)(void *))&worker, &ti[i])) {
      W("T%u pthread_create failed...\n", i);
      exit(EXIT_FAILURE);
    }
  }

  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  epoll_fd = epoll_create1(0);
  ev.events = EPOLLIN | EPOLLOUT; // TODO: check flags

  for (desc = desc_array_zero_terminated, num_descs = 0; desc->process; desc++, num_descs++) {
    desc->_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (desc->_fd == -1) {
      W("Server socket creation failed...\n");
      exit(EXIT_FAILURE);
    }

    addr.sin_addr.s_addr = htonl(desc->ip);
    addr.sin_port = htons(desc->port);

    if ((bind(desc->_fd, (SA*)&addr, sizeof(addr))) != 0) {
      W("Server socket bind failed.\n");
      exit(EXIT_FAILURE);
    }

    //fcntl(desc->_fd, F_SETFL, O_NONBLOCK);
    ev.data.ptr = desc;

    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, desc->_fd, &ev) == -1) {
      W("T%u epoll_ctl: conn_sock\n", i);
      exit(EXIT_FAILURE);
    }

    if ((listen(desc->_fd, MAX_LISTEN_QUEUE)) != 0) {
      W("Listen failed...\n");
      exit(EXIT_FAILURE);
    } else {
      I("Server is listening %u\n", desc->port);
    }
  }

  struct epoll_event* events = (struct epoll_event*)alloca(num_descs * sizeof(struct epoll_event));
  if (events == NULL) {
    W("alloca failed\n");
    exit(EXIT_FAILURE);
  }

  for (i = 0;;) {
    k = epoll_wait(epoll_fd, events, num_descs, -1);
    if (k <= 0) {
      W("Server epoll_wait failed errno: %d\n", errno);
      break;
    }

    for (; k; k--) {
      const struct serv_desc* desc = (const struct serv_desc*)events[k - 1].data.ptr;

      len = sizeof(addr);
      connfd = accept(desc->_fd, (SA*)&addr, &len);

      if (connfd <= 0) {
        W("Server acccept failed errno: %d\n", errno);
          continue;
      } else {
        I("Socket %d accepted\n", connfd);
        do {
          i = (i + 1) % NUM_THREADS; // Fly Robbin, fly.
          j = _semiatomic_pop_slot(ti + i);
        } while (j < 0);

        // if (j < 0) {
        //   close(connfd);
        //   W("Rejecting the client due to server overload\n");
        //   continue;
        // }

        struct client_state *state = &ti[i].cs[j];
        uint8_t* a = (uint8_t*)(&addr.sin_addr.s_addr);
        I("Client %u.%u.%u.%u:%u is taken by worker %u:%u\n", a[0], a[1], a[2], a[3], ntohs(addr.sin_port), i, j);

        fcntl(connfd, F_SETFL, O_NONBLOCK);

        state->_in_use = 1;
        state->fd = connfd;
        // state->ip = cli.sin_addr.s_addr;

        ti[i].ctx[j] = init_ctx(ti[i].stack[j] + (MAX_STACK_SIZE & page_mask), task_wrap, desc->process, ti + i, j);

        ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
        ev.data.ptr = &state->id;

        if (epoll_ctl(ti[i].epoll_fd, EPOLL_CTL_ADD, connfd, &ev) == -1) {
          W("T%u epoll_ctl(EPOLL_CTL_ADD) failed. \n", i);
          close(connfd);
          exit(EXIT_FAILURE);
        }
      }
    }
  }

  for (i = 0; i < NUM_THREADS; i++)
    ti[i].enabled = 0;

  for (i = 0; i < NUM_THREADS; i++) {
    pthread_join(ti[i].tid, NULL);
    close(ti[i].epoll_fd);
    munmap(ti[i].stack_address_space, stacks_map_size);
  }

  close(epoll_fd);
  for (desc = desc_array_zero_terminated; desc->process; desc++)
    close(desc->_fd);
  I("The end\n");
}
