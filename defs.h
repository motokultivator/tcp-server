#include <pthread.h>
#include <stdio.h>
#include <stdint.h>
#include <sys/epoll.h>

#include "config.h"

struct client_state {
  uint32_t id; // 0 This is the index in struct tinfo.cs[].
  int fd;      // 4 Don't change offset of these two fields.
  int last_epoll_fd;
  uint64_t flags; // HI-part flags, LO-part last epoll events
  // unsigned long ip;
  uint8_t _in_use;
};

typedef void (*entry_ptr)(const struct client_state*);

struct serv_desc {
  entry_ptr process;
  uint32_t ip;
  uint16_t port;
  int _fd; // Assigned by server_run().
};

void task_yld();
void task_end();
void server_run(struct serv_desc* desc_array_zero_terminated, int num_threads);

struct tinfo {
  pthread_t tid; // OS thread ID.
  int32_t id; // Internal ID.
  int epoll_fd; // EPOLL file descriptor.
  struct epoll_event events[CLIENTS_PER_THREAD];
  struct client_state cs[CLIENTS_PER_THREAD];
  void* stack_address_space; // the lowest address of map
  void* stack[CLIENTS_PER_THREAD]; // stacks
  void* ctx[CLIENTS_PER_THREAD]; // contexts inside stacks

  volatile uint32_t slots[CLIENTS_PER_THREAD]; // Stack of available task slots. Make sure slots[-1] is accessible.
  volatile int sp; // Points to first empty slot
  volatile uint32_t enabled; // Main loop condition
};
