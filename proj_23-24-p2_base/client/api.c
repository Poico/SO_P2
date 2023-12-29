#include <sys/types.h>
#include <../common/messages.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>

#include "api.h"

static int req_fd, resp_fd;
static int session_id;

int ems_setup(char const* req_pipe_path, char const* resp_pipe_path, char const* server_pipe_path) {
  
  if(mkfifo(req_pipe_path, 0666) == -1) {
    return 1;
  }
  if(mkfifo(resp_pipe_path, 0666) == -1) {
    return 1;
  }
  req_fd = open(req_pipe_path, O_WRONLY);
  if(req_fd == -1) {
    return 1;
  }

  resp_fd = open(resp_pipe_path, O_RDONLY);
  if(resp_fd == -1) {
    return 1;
  }
  
  int server_fd = open(server_pipe_path, O_WRONLY);
  if(server_fd == -1) {
    return 1;
  }
  
  setup_request request;
  request.opcode = MSG_SETUP;
  strncpy(request.request_fifo_name, req_pipe_path, 40);
  strncpy(request.response_fifo_name, resp_pipe_path, 40);
  write(server_fd, &request, sizeof(setup_request));
  
  setup_response response;
  read(resp_fd, &response, sizeof(setup_response));
  
  session_id = response.session_id;
  
  close(server_fd);
  return 0;
}

int ems_quit(void) {
  quit_request request;
  request.opcode = MSG_QUIT;
  write(req_fd, &request, sizeof(quit_request));

  close(req_fd);
  close(resp_fd);
  return 0;
}


int ems_create(unsigned int event_id, size_t num_rows, size_t num_cols) {
  create_request request;
  request.opcode = MSG_CREATE;
  request.event_id = event_id;
  request.num_rows = num_rows;
  request.num_cols = num_cols;

  write(req_fd, &request, sizeof(create_request));

  create_response response;
  read(resp_fd, &response, sizeof(create_response));

  return response.return_code ? 1 : 0;
}

int ems_reserve(unsigned int event_id, size_t num_seats, size_t* xs, size_t* ys) {
  reserve_request request;
  request.opcode = MSG_RESERVE;
  request.event_id = event_id;
  request.num_seats = num_seats;

  write(req_fd, &request, sizeof(reserve_request));
  write(req_fd, xs, num_seats * sizeof(size_t));
  write(req_fd, ys, num_seats * sizeof(size_t));

  reserve_response response;
  read(resp_fd, &response, sizeof(reserve_response));

  return response.return_code ? 1 : 0;
}

int ems_show(int out_fd, unsigned int event_id) {
  show_request request;
  request.opcode = MSG_SHOW;
  request.event_id = event_id;

  write(req_fd, &request, sizeof(show_request));

  show_response response;
  read(resp_fd, &response, sizeof(show_response));

  char buff[1024];
  for (size_t y = 0; y < response.num_rows; y++)
  {
    for (size_t x = 0; x < response.num_cols; x++)
    {
      unsigned int seats;
      read(resp_fd, &seats, sizeof(seats));
      snprintf(buff, 1024, "%u ", seats);
      write(out_fd, buff, strlen(buff));
    }
    write(out_fd, "\n", 1);
  }

  return response.return_code ? 1 : 0;
}

int ems_list_events(int out_fd) {
  list_request request;
  request.opcode = MSG_LIST;

  write(req_fd, &request, sizeof(list_request));

  list_response response;
  read(resp_fd, &response, sizeof(list_response));

  char buff[1024];
  while (response.num_events--)
  {
    unsigned int event_id;
    read(resp_fd, &event_id, sizeof(event_id));
    snprintf(buff, 1024, "Event: %u\n", event_id);
    write(out_fd, buff, strlen(buff));
  }

  return 1;
}
