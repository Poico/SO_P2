#include <asm/signal.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "common/constants.h"
#include "common/io.h"
#include "common/messages.h"
#include "eventlist.h"
#include "operations.h"

int parse_args(int argc, char* argv[]);
void init_server();

void handle_client(int req_fd, int resp_fd);
void close_server();
void handle_SIGUSR1(int signum);
void close_server_threads();

unsigned int state_access_delay_us;
int registerFIFO;
char* FIFO_path;
volatile char server_should_quit;
pthread_t worker_threads[MAX_SESSION_COUNT];

int main(int argc, char* argv[]) {
  int ret = parse_args(argc, argv);
  if (ret) return ret;
  signal(SIGUSR1, handle_SIGUSR1);

  // Initialize EMS
  if (ems_init(state_access_delay_us)) {
    fprintf(stderr, "Failed to initialize EMS\n");
    return 1;
  }

  init_server();

  // TODO: Exit loop condition
  while (!server_should_quit) {
    accept_client();
  }

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
}

void init_server() {
  mkfifo(FIFO_path, 0666);
  registerFIFO = open(FIFO_path, O_RDWR);

  for (int i = 0; i < MAX_SESSION_COUNT; i++) {
    pthread_create(&worker_threads[i], NULL, handle_client, NULL);
  }
  server_should_quit = 0;
}

void accept_client() {
  // TODO: Accept client's pipes (read from main pipe)
  setup_request request;
  if (read(registerFIFO, &request, sizeof(setup_request)) == -1) {
    fprintf(stderr, "Error reading from pipe\n");
    exit(1);
  }

  int req_fd, resp_fd;
  //TODO: Error checking on opens
  req_fd = open(request.request_fifo_name, O_RDONLY);
  resp_fd = open(request.response_fifo_name, O_WRONLY);

  // TODO: Pass to client handler thread (it will take care of the rest)
  handle_client(req_fd, resp_fd);
}

// Each worker thread enters this function once for each client
void handle_client(int req_fd, int resp_fd) {
  //Move signal code to thread init
  sigset_t sigset;
  sigemptyset(&sigset);
  sigaddset(&sigset, SIGUSR1);
  pthread_sigmask(SIG_BLOCK, &sigset, NULL);

  int should_work = 1;

  while (should_work)
  {
    should_work = process_command(req_fd, resp_fd);
  }

  if (close(req_fd) == -1) {
    fprintf(stderr, "Error closing client pipe\n");
    exit(1);
  }
  if (close(resp_fd) == -1) {
    fprintf(stderr, "Error closing client pipe\n");
    exit(1);
  }

  // TODO: Return to "waiting for client" mode
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
  close_server_threads();
  ems_terminate();
  exit(0);
}

void handle_SIGUSR1(int signum) {
  struct EventList* event_list = NULL;
  event_list = get_event_list();

  if (event_list == NULL) {
    fprintf(stderr, "Error getting event list\n");
    return;
  }

  // Loop through all events to memorize their information
  struct ListNode* node = event_list->head;
  while (node != NULL) {
    pthread_mutex_lock(&node->event->mutex);
    ems_show(1, node->event);
    pthread_mutex_unlock(&node->event->mutex);
    node = node->next;
  }
}

void close_server_threads() {
  // TODO: Implement closing server threads
  // Code to close server threads goes here
  // For example, you can use pthread_cancel() to cancel the threads

  // Assuming you have an array of pthread_t for the server threads
  for (int i = 0; i < NUM_SERVER_THREADS; i++) {
    pthread_cancel(server_threads[i]);
  }
}

//Return o for end of processing/error
int process_command(int req_fd, int resp_fd)
{
  // TODO: Error checking
  // TODO: Read request
  core_request core;
  read(req_fd, &core, sizeof(core_request));
  // TODO: Do something with session ID?

  // TODO: Maybe break down into more functions
  switch (core.opcode)
  {
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
      // TODO: Error message?
      return 0;
  }

  // TODO: Process request
  // TODO: Give response to client

  return 1;
}

void handle_create(int req_fd, int resp_fd)
{
  // TODO: Error checking
  create_request req;
  read(req_fd, &req, sizeof(create_request));

  int ret = ems_create(req.event_id, req.num_rows, req.num_cols);

  create_response resp = { .return_code = ret };
  write(resp_fd, &resp, sizeof(create_response));
}

void handle_reserve(int req_fd, int resp_fd)
{
  // TODO: Error checking
  reserve_request req;
  read(req_fd, &req, sizeof(reserve_request));

  size_t *xs = malloc(req.num_seats * sizeof(size_t));
  size_t *ys = malloc(req.num_seats * sizeof(size_t));

  read(req_fd, xs, req.num_seats * sizeof(size_t));
  read(req_fd, ys, req.num_seats * sizeof(size_t));

  int ret = ems_reserve(req.event_id, req.num_seats, xs, ys);

  reserve_response resp = { .return_code = ret };
  write(resp_fd, &resp, sizeof(reserve_response));
}

void handle_show(int req_fd, int resp_fd)
{
  // TODO: Error checking
  show_request req;
  read(req_fd, &req, sizeof(show_request));

  // TODO: Process show

  show_response resp;
  resp.num_cols = ;
  resp.num_rows = ;
  resp.return_code = ;
  write(resp_fd, &resp, sizeof(show_response));
  //TODO: Write data
}

void handle_list(int req_fd, int resp_fd)
{
  // TODO: Error checking
  //No need to read request, has no extra data

  //TODO: Process list

  list_response resp;
  resp.num_events = ;
  resp.return_code = ;
  write(resp_fd, &resp, sizeof(list_response));
  //TODO: Write data
}
