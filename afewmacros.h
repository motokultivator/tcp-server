#define ERR(k) (k <= 0 && (k != -1 || (errno != EAGAIN && errno != EWOULDBLOCK)))

#define ASSERT(k, __fd) if (k <= 0) {                        \
  if (k == -1 && (errno == EAGAIN || errno == EWOULDBLOCK))  \
    k = 0;                                                   \
  else RET;                                                  \
  }

#define _READN(__read, __assert, __fd, __buf, __len)               \
  for (int __k, __l = 0;;) {                                       \
    __k = read(__fd, __buf + __l, __len - __l);                    \
    ASSERT(__k, __fd);                                             \
    __l += __k;                                                    \
    if (__l < __len) YLD(); else break;                            \
  }

#define _READSEP(__read, __assert, __l, __sptr, __fd, __buf, __len, __sep, __sep_len) \
  __l = 0;                                                                            \
  for (int __k, __t;;) {                                                              \
    __k = __read(__fd, __buf + __l, __len - __l - 1);                                 \
    __assert(__k, __fd);                                                              \
    if (__l + __k < __sep_len) { YLD(); continue; }                                   \
    *(char*)(__buf + __l + __k) = 0;                                                  \
    __t = __l - __sep_len + 1; if (__t < 0) __t = 0;                                  \
    __l += __k;                                                                       \
    if (__sptr = strstr((const char*)(__buf + __t), __sep)) break;                    \
    if (__l < __len - 1) YLD(); else RET;                                             \
  }

#define _WRITEN(__write, __assert, __fd, __buf, __len) \
  for (int __k, __l = 0;;) {                           \
    __k = write(__fd, __buf + __l, __len - __l);       \
    __assert(__k, __fd);                               \
    __l += __k;                                        \
    if (__l < __len) YLD(); else break;                \
  }

// RET() and YLD() should be defined

// Read exactely __len bytes into buf or RET
#define READN(__fd, __buf, __len) _READN(read, ASSERT, __fd, __buf, __len)

// Write exactely _len bytes from buf or RET
#define WRITEN(__fd, __buf, __len) _WRITEN(write, ASSERT, __fd, __buf, __len)

// Read until __sep is present or RET
#define READSEP(__l, __sptr, __fd, __buf, __len, __sep, __sep_len) _READSEP(read, ASSERT, __l, __sptr, __fd, __buf, __len, __sep, __sep_len)
