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
#include "operations.h"
#include "eventlist.h"

int parse_args(int argc, char* argv[]);
void init_server();

void handle_client();
void close_server();
void handle_SIGUSR1(int signum);
void close_server_threads();

unsigned int state_access_delay_us;
int registerFIFO;
char* FIFO_path;
volatile char server_should_quit;

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
  // LATER: TODO: Initialize worker threads
  server_should_quit = 0;
}

void accept_client() {
  // TODO: Accept client's pipes (read from main pipe)
  request request;
  if (read(registerFIFO, &request, sizeof(request)) == -1) {
    fprintf(stderr, "Error reading from pipe\n");
    exit(1);
  }
  request.session_id = 1;

  // TODO: Pass to client handler thread (it will take care of the rest)
  handle_client();

  // TODO: Write new client to the producer-consumer buffer
}

// Each worker thread enters this function once for each client
void handle_client() {
  sigset_t sigset;
  sigemptyset(&sigset);
  sigaddset(&sigset, SIGUSR1);
  pthread_sigmask(SIG_BLOCK, &sigset, NULL);
  // TODO: Open pipes provided by client

  // TODO: Work loop
  // TODO: Read request
  // TODO: Process request
  // TODO: Give response to client
  // TODO: Close pipes
  if (close(client_pipe) == -1) {
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
