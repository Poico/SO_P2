#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>

#include "common/constants.h"
#include "common/io.h"
#include "common/messages.h"
#include "eventlist.h"
#include "operations.h"

#include <errno.h>

int parse_args(int argc, char* argv[]);
void init_server();

void accept_client();
void handle_client(int req_fd, int resp_fd);
void close_server();
void handle_SIGUSR1(int signum);
void close_server_threads();
int process_command(int req_fd, int resp_fd);
void handle_create(int req_fd, int resp_fd);
void handle_reserve(int req_fd, int resp_fd);
void handle_show(int req_fd, int resp_fd);
void handle_list(int req_fd, int resp_fd);

unsigned int state_access_delay_us;
int registerFIFO;
char* FIFO_path;
volatile char server_should_quit;
pthread_t worker_threads[MAX_SESSION_COUNT];

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
  // TODO: Exit loop condition
  while (!server_should_quit) {
    accept_client();
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

  //for (int i = 0; i < MAX_SESSION_COUNT; i++) {
  //  pthread_create(&worker_threads[i], NULL, handle_client, NULL);
  //}
  server_should_quit = 0;
}

void accept_client() {
  // TODO: Error checking on read
  char opcode;
  read(registerFIFO, &opcode, sizeof(char));
  if (opcode != MSG_SETUP)
  {
    //TODO: Error
  }

  setup_request request;
  if (read(registerFIFO, &request, sizeof(setup_request)) == -1) {
    fprintf(stderr, "Error reading from pipe\n");
    exit(1);
  }

  int req_fd, resp_fd;
  //TODO: Error checking on opens
  printf("Opening pipes.\n");
  req_fd = open(request.request_fifo_name, O_RDONLY);
  resp_fd = open(request.response_fifo_name, O_WRONLY);

  printf("Handle client.\n");
  handle_client(req_fd, resp_fd);
}

// Each worker thread enters this function once for each client
void handle_client(int req_fd, int resp_fd) {
  //Move signal code to thread init
  //sigset_t sigset;
  //sigemptyset(&sigset);
  //sigaddset(&sigset, SIGUSR1);
  //pthread_sigmask(SIG_BLOCK, &sigset, NULL);

  //TODO: Set actual session id
  setup_response resp = { .session_id = 0 };
  //TODO: Error checking
  printf("Sending first response.\n");
  write(resp_fd, &resp, sizeof(setup_response));

  int should_work = 1;

  while (should_work)
  {
    printf("Processing command.\n");
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
  // TODO (Once threaded): Return to "waiting for client" mode
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
  (void)signum;
  //TODO: Rewrite to only set a flag and do the printing in main

  /*struct EventList* event_list = NULL;
  event_list = get_event_list();

  if (event_list == NULL) {
    fprintf(stderr, "Error getting event list\n");
    return;
  }

  // Loop through all events to memorize their information
  struct ListNode* node = event_list->head;
  while (node != NULL) {
    pthread_mutex_lock(&node->event->mutex);
    ems_show(1,node->event->id);
    pthread_mutex_unlock(&node->event->mutex);
    node = node->next;
  }*/
}

void close_server_threads() {
  // Code to close server threads goes here
  // For example, you can use pthread_cancel() to cancel the threads

  // Assuming you have an array of pthread_t for the server threads
  //for (int i = 0; i < NUM_SERVER_THREADS; i++) {
  //  pthread_cancel(server_threads[i]);
  //}
}

//Return o for end of processing/error
int process_command(int req_fd, int resp_fd)
{
  core_request core;
  if(read(req_fd, &core, sizeof(core_request)) == -1)
  {
    fprintf(stderr, "Error reading from pipe.\n");
    fprintf(stderr, "Errno: %d\n", errno);
    exit(1);
  }
  // TODO: Do something with session ID?

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
      fprintf(stderr, "Invalid opcode\n");
      return 0;
  }

  return 1;
}

void handle_create(int req_fd, int resp_fd)
{
  create_request req;
  if(read(req_fd, &req, sizeof(create_request)) == -1)
  {
    fprintf(stderr, "Error reading from pipe\n");
    exit(1);
  }

  int ret = ems_create(req.event_id, req.num_rows, req.num_cols);

  create_response resp = { .return_code = ret };
  if(write(resp_fd, &resp, sizeof(create_response)) == -1)
  {
    fprintf(stderr, "Error writing to pipe\n");
    exit(1);
  }
}

void handle_reserve(int req_fd, int resp_fd)
{
  reserve_request req;
  if(read(req_fd, &req, sizeof(reserve_request)) == -1)
  {
    fprintf(stderr, "Error reading from pipe\n");
    exit(1);
  }

  size_t *xs = malloc(req.num_seats * sizeof(size_t));
  size_t *ys = malloc(req.num_seats * sizeof(size_t));
  if(read(req_fd, xs, req.num_seats * sizeof(size_t)) == -1)
  {
    fprintf(stderr, "Error reading from pipe\n");
    exit(1);
  }

  if(read(req_fd, ys, req.num_seats * sizeof(size_t)) == -1)
  {
    fprintf(stderr, "Error reading from pipe\n");
    exit(1);
  }

  int ret = ems_reserve(req.event_id, req.num_seats, xs, ys);

  reserve_response resp = { .return_code = ret };
  if(write(resp_fd, &resp, sizeof(reserve_response)) == -1)
  {
    fprintf(stderr, "Error writing to pipe\n");
    exit(1);
  }
}

void handle_show(int req_fd, int resp_fd)
{
  show_request req;
  if(read(req_fd, &req, sizeof(show_request)) == -1)
  {
    fprintf(stderr, "Error reading from pipe\n");
    exit(1);
  }

  size_t rows = 0, cols = 0;
  unsigned int *data = ems_show_to_client(req.event_id, &rows, &cols);

  show_response resp;
  resp.num_cols = cols;
  resp.num_rows = rows;
  resp.return_code = data == NULL ? 1 : 0;

  if(write(resp_fd, &resp, sizeof(show_response)) == -1)
  {
    fprintf(stderr, "Error writing to pipe\n");
    exit(1);
  }
  
  //TODO: Error checking
  write(resp_fd, data, sizeof(unsigned int) * rows * cols);
}

void handle_list(int req_fd, int resp_fd)
{
  //No need to read request, has no extra data
  (void)req_fd;

  size_t event_count = 0;
  unsigned int *data = ems_list_events_to_client(&event_count);

  list_response resp;
  resp.num_events = event_count;
  resp.return_code = data == NULL ? 1 : 0;
  if(write(resp_fd, &resp, sizeof(list_response)) == -1)
  {
    fprintf(stderr, "Error writing to pipe\n");
    exit(1);
  }

  //TODO: Error checking
  write(resp_fd, data, sizeof(unsigned int) * event_count);
}
