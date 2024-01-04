#include "api.h"

#include <common/messages.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static int req_fd, resp_fd;
static int session_id;

#define SEND_CORE(op) do { core_request core = { .opcode = op, .session_id = session_id }; if (write(req_fd, &core, sizeof(core_request)) == -1) { return 1; }} while(0)

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

  char opcode = MSG_SETUP;
  if (write(server_fd, &opcode, sizeof(char)) == -1) {
    return 1;
  }

  setup_request request;
  memset(request.request_fifo_name, 0, 40);
  memset(request.response_fifo_name, 0, 40);
  strncpy(request.request_fifo_name, req_pipe_path, 40);
  strncpy(request.response_fifo_name, resp_pipe_path, 40);
  if (write(server_fd, &request, sizeof(setup_request)) == -1) {
    return 1;
  }
  
  setup_response response;
  if (read(resp_fd, &response, sizeof(setup_response)) == -1) {
    return 1;
  }
  
  session_id = response.session_id;

  if (close(server_fd) == -1) {
    return 1;
  }
  return 0;
}

int ems_quit(void) {
  SEND_CORE(MSG_QUIT);

  if (close(req_fd) == -1) {
    return 1;
  }
  if (close(resp_fd) == -1) {
    return 1;
  }
  return 0;
}

int ems_create(unsigned int event_id, size_t num_rows, size_t num_cols) {
  SEND_CORE(MSG_CREATE);

  create_request request;
  request.event_id = event_id;
  request.num_rows = num_rows;
  request.num_cols = num_cols;

  if (write(req_fd, &request, sizeof(create_request)) == -1) {
    return 1;
  }

  create_response response;
  if (read(resp_fd, &response, sizeof(create_response)) == -1) {
    return 1;
  }

  return response.return_code ? 1 : 0;
}

int ems_reserve(unsigned int event_id, size_t num_seats, size_t* xs, size_t* ys) {
  SEND_CORE(MSG_RESERVE);
  
  reserve_request request;
  request.event_id = event_id;
  request.num_seats = num_seats;

  if (write(req_fd, &request, sizeof(reserve_request)) == -1) {
    return 1;
  }
  if (write(req_fd, xs, num_seats * sizeof(size_t)) == -1) {
    return 1;
  }
  if (write(req_fd, ys, num_seats * sizeof(size_t)) == -1) {
    return 1;
  }

  reserve_response response;
  if (read(resp_fd, &response, sizeof(reserve_response)) == -1) {
    return 1;
  }

  return response.return_code ? 1 : 0;
}

int ems_show(int out_fd, unsigned int event_id) {
  SEND_CORE(MSG_SHOW);

  show_request request;
  request.event_id = event_id;

  if (write(req_fd, &request, sizeof(show_request)) == -1) {
    return 1;
  }

  show_response response;
  if (read(resp_fd, &response, sizeof(show_response)) == -1) {
    return 1;
  }

  char buff[1024];
  for (size_t y = 0; y < response.num_rows; y++)
  {
    for (size_t x = 0; x < response.num_cols; x++)
    {
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

  return response.return_code ? 1 : 0;
}

int ems_list_events(int out_fd) {
  SEND_CORE(MSG_LIST);

  list_response response;
  if(read(resp_fd, &response, sizeof(list_response)) == -1) {
    return 1;
  }

  char buff[1024];
  while (response.num_events--)
  {
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
