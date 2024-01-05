#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "common/constants.h"
#include "common/io.h"
#include "common/messages.h"
#include "eventlist.h"
#include "operations.h"

#define BUFFER_SIZE 4

int parse_args(int argc, char* argv[]);
void init_server();

void accept_client();
void handle_client(unsigned int session_id, int req_fd, int resp_fd);
void close_server();
void handle_SIGUSR1(int signum);
void close_server_threads();
int process_command(int req_fd, int resp_fd);
void handle_create(int req_fd, int resp_fd);
void handle_reserve(int req_fd, int resp_fd);
void handle_show(int req_fd, int resp_fd);
void handle_list(int req_fd, int resp_fd);
void* worker_thread_main(void* arg);

unsigned int state_access_delay_us;
int registerFIFO;
char* FIFO_path;
volatile char server_should_quit;

pthread_t worker_threads[MAX_SESSION_COUNT];
unsigned int thread_args[MAX_SESSION_COUNT];

// Buffer to hold client requests
setup_request buffer[BUFFER_SIZE];
int in = 0;
int out = 0;

pthread_mutex_t buffer_mutex;
pthread_cond_t buffer_not_full;
pthread_cond_t buffer_not_empty;

volatile char show_flag = 0;  // no need for thread safety, not important

int main(int argc, char* argv[]) {
  int ret = parse_args(argc, argv);
  if (ret) return ret;

  signal(SIGUSR1, handle_SIGUSR1);

  printf("Init EMS.\n");
  // Initialize EMS
  if (ems_init(state_access_delay_us)) {
    fprintf(stderr, "Failed to initialize EMS\n");
    return 1;
  }

  printf("Init server.\n");
  init_server();

  printf("Work loop.\n");
  while (!server_should_quit) {
    accept_client();

    if (show_flag) {
      show_flag = 0;

      size_t count;
      unsigned int* data = ems_list_events_to_client(&count);

      if (data == NULL) continue;

      write(1, "Listing all events:\n", 20);
      for (size_t i = 0; i < count; i++) {
        char buff[32];
        snprintf(buff, 32, "Event %d:\n", data[i]);
        write(1, buff, strlen(buff));
        ems_show(1, data[i]);
      }
    }
  }

  printf("Close server.\n");
  close_server();
}

int parse_args(int argc, char* argv[]) {
  if (argc < 2 || argc > 3) {
    fprintf(stderr, "Usage: %s\n <pipe_path> [delay]\n", argv[0]);
    return 1;
  }

  char* endptr;
  state_access_delay_us = STATE_ACCESS_DELAY_US;
  if (argc == 3) {
    unsigned long int delay = strtoul(argv[2], &endptr, 10);

    if (*endptr != '\0' || delay > UINT_MAX) {
      fprintf(stderr, "Invalid delay value or value too large\n");
      return 1;
    }

    state_access_delay_us = (unsigned int)delay;
  }
  if (argc >= 2) {
    FIFO_path = argv[1];
  }

  return 0;
}

void init_server() {
  unlink(FIFO_path);
  mkfifo(FIFO_path, 0666);
  registerFIFO = open(FIFO_path, O_RDWR);

  pthread_mutex_init(&buffer_mutex, NULL);
  pthread_cond_init(&buffer_not_full, NULL);
  pthread_cond_init(&buffer_not_empty, NULL);

  for (int i = 0; i < MAX_SESSION_COUNT; i++) {
    thread_args[i] = (unsigned int)i;
    pthread_create(&worker_threads[i], NULL, worker_thread_main, &thread_args[i]);
  }
  server_should_quit = 0;
}

void accept_client() {
  char opcode;
  if (read(registerFIFO, &opcode, sizeof(char)) == -1) {
    if (errno == 4)  // Interrupted system call
    {
      return;
    }  // ignore
    fprintf(stderr, "Error reading from pipe while accepting client: %d.\n", errno);
    exit(1);
  }

  setup_request request;
  if (read(registerFIFO, &request, sizeof(setup_request)) == -1) {
    fprintf(stderr, "Error reading from pipe while accepting client paths: %d.\n", errno);
    exit(1);
  }

  printf("Producer is producing...\n");
  pthread_mutex_lock(&buffer_mutex);
  // Wait for buffer to not be full
  // TODO: the problem is that I'm adding in+1 and not keep tracking of the number of elements
  while ((in + 1) % BUFFER_SIZE == out) pthread_cond_wait(&buffer_not_full, &buffer_mutex);
  // Add request to buffer
  buffer[in] = request;
  in = (in + 1) % BUFFER_SIZE;
  // Signal that buffer is not empty
  pthread_cond_signal(&buffer_not_empty);
  // Unlock mutex
  pthread_mutex_unlock(&buffer_mutex);

  printf("Producer produced.\n");
}

void* worker_thread_main(void* arg) {
  printf("Thread start.\n");
  unsigned int session_id = *((unsigned int*)arg);

  sigset_t sigset;
  sigemptyset(&sigset);
  sigaddset(&sigset, SIGUSR1);
  pthread_sigmask(SIG_BLOCK, &sigset, NULL);

  while (1) {
    pthread_mutex_lock(&buffer_mutex);
    while (in == out) pthread_cond_wait(&buffer_not_empty, &buffer_mutex);
    setup_request request = buffer[out];
    out = (out + 1) % BUFFER_SIZE;

    printf("Consumer is consuming...\n");
    out = (out + 1) % BUFFER_SIZE;
    // Signal that buffer is not full
    pthread_cond_signal(&buffer_not_full);
    // Unlock mutex
    pthread_mutex_unlock(&buffer_mutex);
    printf("Consumer consumed.\n");

    printf("Received paths '%s' and '%s'.\n", request.request_fifo_name, request.response_fifo_name);

    int req_fd, resp_fd;

    if ((req_fd = open(request.request_fifo_name, O_RDONLY)) == -1) {
      fprintf(stderr, "Error opening request pipe\n");
      exit(1);
    }
    if ((resp_fd = open(request.response_fifo_name, O_WRONLY)) == -1) {
      fprintf(stderr, "Error opening response pipe\n");
      exit(1);
    }

    handle_client(session_id, req_fd, resp_fd);
  }
}

// Each worker thread enters this function once for each client
void handle_client(unsigned int session_id, int req_fd, int resp_fd) {
  setup_response resp = {.session_id = session_id};

  if (write(resp_fd, &resp, sizeof(setup_response)) == -1) {
    fprintf(stderr, "Error writing to pipe\n");
    exit(1);
  }

  int should_work = 1;

  while (should_work) {
    should_work = process_command(req_fd, resp_fd);
  }

  printf("Done with client.\n");

  if (close(req_fd) == -1) {
    fprintf(stderr, "Error closing client pipe\n");
    exit(1);
  }
  if (close(resp_fd) == -1) {
    fprintf(stderr, "Error closing client pipe\n");
    exit(1);
  }

  printf("Going to sleep.\n");
}

void close_server() {
  if (unlink(FIFO_path) == -1) {
    fprintf(stderr, "Error deleting register FIFO\n");
    exit(1);
  }
  if (close(registerFIFO) == -1) {
    fprintf(stderr, "Error closing register FIFO\n");
    exit(1);
  }
  pthread_mutex_destroy(&buffer_mutex);
  pthread_cond_destroy(&buffer_not_full);
  pthread_cond_destroy(&buffer_not_empty);
  close_server_threads();
  ems_terminate();
  exit(0);
}

void handle_SIGUSR1(int signum) {
  (void)signum;
  show_flag = 1;

  printf("USR1\n");

  signal(SIGUSR1, handle_SIGUSR1);
}

void close_server_threads() {
  for (int i = 0; i < MAX_SESSION_COUNT; i++) {
    pthread_cancel(worker_threads[i]);
  }
}

int process_command(int req_fd, int resp_fd) {
  core_request core;
  if (read(req_fd, &core, sizeof(core_request)) == -1) {
    fprintf(stderr, "Error reading from pipe while reading core: %d.\n", errno);
    exit(1);
  }

  switch (core.opcode) {
    case MSG_QUIT:
      return 0;

    case MSG_CREATE:
      handle_create(req_fd, resp_fd);
      break;

    case MSG_RESERVE:
      handle_reserve(req_fd, resp_fd);
      break;

    case MSG_SHOW:
      handle_show(req_fd, resp_fd);
      break;

    case MSG_LIST:
      break;

    case MSG_SETUP:
    default:
      fprintf(stderr, "Invalid opcode\n");
      return 0;
  }

  return 1;
}

void handle_create(int req_fd, int resp_fd) {
  create_request req;
  if (read(req_fd, &req, sizeof(create_request)) == -1) {
    fprintf(stderr, "Error reading from pipe\n");
    exit(1);
  }

  int ret = ems_create(req.event_id, req.num_rows, req.num_cols);

  create_response resp = {.return_code = ret};
  if (write(resp_fd, &resp, sizeof(create_response)) == -1) {
    fprintf(stderr, "Error writing to pipe\n");
    exit(1);
  }
}

void handle_reserve(int req_fd, int resp_fd) {
  reserve_request req;
  if (read(req_fd, &req, sizeof(reserve_request)) == -1) {
    fprintf(stderr, "Error reading from pipe\n");
    exit(1);
  }

  size_t* xs = malloc(req.num_seats * sizeof(size_t));
  size_t* ys = malloc(req.num_seats * sizeof(size_t));
  if (read(req_fd, xs, req.num_seats * sizeof(size_t)) == -1) {
    fprintf(stderr, "Error reading from pipe\n");
    free(xs);  // If this fails, we need to free xs and ys before exiting
    free(ys);
    exit(1);
  }

  if (read(req_fd, ys, req.num_seats * sizeof(size_t)) == -1) {
    fprintf(stderr, "Error reading from pipe\n");
    free(xs);
    free(ys);
    exit(1);
  }

  int ret = ems_reserve(req.event_id, req.num_seats, xs, ys);

  free(xs);
  free(ys);

  reserve_response resp = {.return_code = ret};
  if (write(resp_fd, &resp, sizeof(reserve_response)) == -1) {
    fprintf(stderr, "Error writing to pipe\n");
    exit(1);
  }
}

void handle_show(int req_fd, int resp_fd) {
  show_request req;
  if (read(req_fd, &req, sizeof(show_request)) == -1) {
    fprintf(stderr, "Error reading from pipe\n");
    exit(1);
  }

  size_t rows = 0, cols = 0;
  unsigned int* data = ems_show_to_client(req.event_id, &rows, &cols);

  show_response resp;
  resp.num_cols = cols;
  resp.num_rows = rows;
  resp.return_code = data == NULL ? 1 : 0;

  if (write(resp_fd, &resp, sizeof(show_response)) == -1) {
    fprintf(stderr, "Error writing to pipe\n");
    free(data);
    exit(1);
  }

  if (write(resp_fd, data, sizeof(unsigned int) * rows * cols) == -1) {
    fprintf(stderr, "Error writing to pipe\n");
    free(data);
    exit(1);
  }

  free(data);
}

void handle_list(int req_fd, int resp_fd) {
  // No need to read request, has no extra data
  (void)req_fd;

  size_t event_count = 0;
  unsigned int* data = ems_list_events_to_client(&event_count);

  list_response resp;
  resp.num_events = event_count;
  resp.return_code = data == NULL ? 1 : 0;
  if (write(resp_fd, &resp, sizeof(list_response)) == -1) {
    fprintf(stderr, "Error writing to pipe\n");
    free(data);
    exit(1);
  }

  if (write(resp_fd, data, event_count * sizeof(unsigned int)) == -1) {
    fprintf(stderr, "Error writing to pipe\n");
    free(data);
    exit(1);
  }

  free(data);
}
