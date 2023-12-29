#include "api.h"

#include <sys/types.h>
#include <../common/messages.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

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
  
  setup_message msg;
  msg.opcode = MSG_SETUP;
  strncpy(msg.request_fifo_name, req_pipe_path, 40);
  strncpy(msg.response_fifo_name, resp_pipe_path, 40);
  write(server_fd, &msg, sizeof(setup_message));
  
  setup_response response;
  read(resp_fd, &response, sizeof(setup_response));
  
  session_id = response.session_id;
  
  close(server_fd);
  return 0;
}

int ems_quit(void) {
  quit_message msg;
  msg.opcode = MSG_QUIT;
  write(req_fd, &msg, sizeof(quit_message));

  close(req_fd);
  close(resp_fd);
  return 0;
}


int ems_create(unsigned int event_id, size_t num_rows, size_t num_cols) {
  create_message msg;
  msg.opcode = MSG_CREATE;
  msg.event_id = event_id;
  msg.num_rows = num_rows;
  msg.num_cols = num_cols;

  write(req_fd, &msg, sizeof(create_message));

  create_response response;
  read(resp_fd, &response, sizeof(create_response));

  return response.return_code ? 1 : 0;
}

int ems_reserve(unsigned int event_id, size_t num_seats, size_t* xs, size_t* ys) {
  //TODO: send reserve request to the server (through the request pipe)

  reserve_response response;
  read(resp_fd, &response, sizeof(reserve_response));

  return response.return_code ? 1 : 0;
}

int ems_show(int out_fd, unsigned int event_id) {
  show_message msg;
  msg.opcode = MSG_SHOW;
  msg.event_id = event_id;

  write(req_fd, &msg, sizeof(show_message));

  //CANT DO YET: TODO: wait for the response (through the response pipe)
  return 1;
}

int ems_list_events(int out_fd) {
  list_message msg;
  msg.opcode = MSG_LIST;

  write(req_fd, &msg, sizeof(list_message));

  //CANT DO YET: TODO: wait for the response (through the response pipe)
  return 1;
}
