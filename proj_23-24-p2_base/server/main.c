#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

#include "common/constants.h"
#include "common/io.h"
#include "operations.h"
void handle_client(int *pipes);
void close_server(int *pipes);

int main(int argc, char* argv[]) {
  if (argc < 2 || argc > 3) {
    fprintf(stderr, "Usage: %s\n <pipe_path> [delay]\n", argv[0]);
    return 1;
  }

  char* endptr;
  unsigned int state_access_delay_us = STATE_ACCESS_DELAY_US;
  if (argc == 3) {
    unsigned long int delay = strtoul(argv[2], &endptr, 10);

    if (*endptr != '\0' || delay > UINT_MAX) {
      fprintf(stderr, "Invalid delay value or value too large\n");
      return 1;
    }

    state_access_delay_us = (unsigned int)delay;
  }

  if (ems_init(state_access_delay_us)) {
    fprintf(stderr, "Failed to initialize EMS\n");
    return 1;
  }

  // IGNORED FOR NOW: TODO: Intialize server, create worker threads
  int* pipeParentToChild = malloc(sizeof(int) * 2);
  int* pipeChildToParent = malloc(sizeof(int) * 2);
  while (1) {
    handle_client(pipeChildToParent);
  }

  
  close_server(pipes);
}

void handle_client(int* pipes, int* request) {
  // TODO: Read from pipe
  if(read(pipes[0], &request, sizeof(request)) == -1){
    fprintf(stderr, "Error reading from pipe\n");
    exit(1);
  }
  // TODO: Write new client to the producer-consumer buffer
}

void close_server(int* pipes){
  if(close(pipes[0]) == -1){
    fprintf(stderr, "Error closing pipe 0\n");
    exit(1);
  }
  ems_terminate();
  exit(0);
}