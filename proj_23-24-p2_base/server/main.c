#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "common/constants.h"
#include "common/io.h"
#include "operations.h"

int parse_args(int argc, char* argv[]);
void init_server();

void handle_client();
void close_server();

unsigned int state_access_delay_us;
int registerFIFO;
char *FIFO_path;
volatile char server_should_quit;

int main(int argc, char* argv[]) {
  int ret = parse_args(argc, argv);
  if (ret) return ret;

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
  if (argc >= 2)
  {
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
  int client_pipe;
  if (read(registerFIFO, &client_pipe, sizeof(int)) == -1) {
    fprintf(stderr, "Error reading client pipe from main pipe\n");
    exit(1);
  }

  // TODO: Pass to client handler thread (it will take care of the rest)
  // create_thread(client_handler_thread, &client_pipe);

  // TODO: Write new client to the producer-consumer buffer
}

// Each worker thread enters this function once for each client
void handle_client() {
  // TODO: Open pipes provided by client
  int client_pipe = open(FIFO_path, O_RDWR);
  if (client_pipe == -1) {
    fprintf(stderr, "Error opening client pipe\n");
    exit(1);
  }

  // TODO: Work loop
  while (1) {
    // TODO: Read request
    Request request;
    if (read(client_pipe, &request, sizeof(Request)) == -1) {
      fprintf(stderr, "Error reading request from client\n");
      exit(1);
    }

    // TODO: Process request
    // process_request(request);

    // TODO: Give response to client
    // Response response = create_response();
    // write(client_pipe, &response, sizeof(Response));

    // TODO: Check if client wants to exit
    if (request.type == EXIT_REQUEST) {
      break;
    }
  }

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
  // LATER: TODO: Close server threads
  ems_terminate();
  exit(0);
}
