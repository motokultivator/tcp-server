#define DEFAULT_NUM_THREADS  4
#define CLIENTS_PER_THREAD   100
#define MAX_STACK_SIZE       (1024 * 1024 * 10)
#define MIN_STACK_SIZE       4096
#define RED_ZONE_STACK_SIZE  65536

#define MAX_LISTEN_QUEUE     10
#define POLL_TIMEOUT         5000
#define CLIENT_BUFFER_SIZE   8192

#define I(...) do {} while(0);
#define W(...) do {} while(0);

// #define W printf
// #define I printf
