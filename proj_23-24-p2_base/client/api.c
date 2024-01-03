#include "api.h"

#include <../common/messages.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static int req_fd, resp_fd;
static int session_id;

int ems_setup(char const* req_pipe_path, char const* resp_pipe_path, char const* server_pipe_path) {
  if (mkfifo(req_pipe_path, 0666) == -1) {
    return 1;
  }

  if (mkfifo(resp_pipe_path, 0666) == -1) {
    return 1;
  }

  if(access(req_pipe_path, F_OK) == -1) {
    return 1;
  }

  req_fd = open(req_pipe_path, O_WRONLY);
  if (req_fd == -1) {
    return 1;
  }

  if(access(resp_pipe_path, F_OK) == -1) {
    return 1;
  }
  resp_fd = open(resp_pipe_path, O_RDONLY);
  
  if (resp_fd == -1) {
    return 1;
  }

  if(access(server_pipe_path, F_OK) == -1) {
    return 1;
  }
  int server_fd = open(server_pipe_path, O_WRONLY);
  if (server_fd == -1) {
    return 1;
  }

  request request;
  request.opcode = MSG_SETUP;
  strncpy(request.setup.request_fifo_name, req_pipe_path, 40);
  strncpy(request.setup.response_fifo_name, resp_pipe_path, 40);

  if (write(server_fd, &request, sizeof(request)) == -1) {
    return 1;
  }

  response response;
  if (read(resp_fd, &response, sizeof(response)) == -1) {
    return 1;
  }

  session_id = response.setup.session_id;
  if (close(server_fd) == -1) {
    return 1;
  }
  return 0;
}

int ems_quit(void) {
  request request;
  request.opcode = MSG_QUIT;
  if (write(req_fd, &request, sizeof(request)) == -1) {
    return 1;
  }

  if (close(req_fd) == -1) {
    return 1;
  }
  if (close(resp_fd) == -1) {
    return 1;
  }
  return 0;
}

int ems_create(unsigned int event_id, size_t num_rows, size_t num_cols) {
  request request;
  request.opcode = MSG_CREATE;
  request.create.event_id = event_id;
  request.create.num_rows = num_rows;
  request.create.num_cols = num_cols;
  if (write(req_fd, &request, sizeof(request)) == -1) {
    return 1;
  }

  response response;
  if (read(resp_fd, &response, sizeof(response)) == -1) {
    return 1;
  }

  return response.create.return_code ? 1 : 0;
}

int ems_reserve(unsigned int event_id, size_t num_seats, size_t* xs, size_t* ys) {
  request request;
  request.opcode = MSG_RESERVE;
  request.reserve.event_id = event_id;
  request.reserve.num_seats = num_seats;

  if (write(req_fd, &request, sizeof(request)) == -1) {
    return 1;
  }
  if (write(req_fd, xs, num_seats * sizeof(size_t)) == -1) {
    return 1;
  }
  if (write(req_fd, ys, num_seats * sizeof(size_t)) == -1) {
    return 1;
  }

  response response;
  if (read(resp_fd, &response, sizeof(response)) == -1) {
    return 1;
  }

  return response.create.return_code ? 1 : 0;
}

int ems_show(int out_fd, unsigned int event_id) {
  request request;
  request.opcode = MSG_SHOW;
  request.show.event_id = event_id;
  if (write(req_fd, &request, sizeof(request)) == -1) {
    return 1;
  }

  response response;
  if (read(resp_fd, &response, sizeof(response)) == -1) {
    return 1;
  }

  char buff[1024];
  for (size_t y = 0; y < response.show.num_rows; y++) {
    for (size_t x = 0; x < response.show.num_cols; x++) {
      unsigned int seats;
      if (read(resp_fd, &seats, sizeof(seats)) == -1) {
        return 1;
      }
      snprintf(buff, 1024, "%u ", seats);
      if (write(out_fd, buff, strlen(buff)) == -1) {
        return 1;
      }
    }
    if (write(out_fd, "\n", 1) == -1) {
      return 1;
    }
  }

  return response.show.return_code ? 1 : 0;
}

int ems_list_events(int out_fd) {
  request request;
  request.opcode = MSG_LIST;
  if(write(req_fd, &request, sizeof(request)) == -1) {
    return 1;
  }

  response response;
  if(read(resp_fd, &response, sizeof(response)) == -1) {
    return 1;
  }

  char buff[1024];
  while (response.list.num_events--) {
    unsigned int event_id;
    if(read(resp_fd, &event_id, sizeof(event_id)) == -1) {
      return 1;
    }
    snprintf(buff, 1024, "Event: %u\n", event_id);
    if(write(out_fd, buff, strlen(buff)) == -1) {
      return 1;
    }
  }

  return 1;
}
