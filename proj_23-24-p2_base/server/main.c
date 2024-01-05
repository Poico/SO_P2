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

//===Internal function declarations===
int parse_args(int argc, char* argv[]);
int init_server();
void accept_client();
void handle_client(unsigned int session_id, int req_fd, int resp_fd);
void close_server();
void handle_SIGUSR1(int signum);
void handle_SIGINT(int signum);
int process_command(int req_fd, int resp_fd);
setup_request buffer_get();
void buffer_add(setup_request request);

//===Parsed arguments===
unsigned int state_access_delay_us;
char* FIFO_path;

//===Server state and flags===
int registerFIFO;
volatile char server_should_quit;
volatile char show_flag = 0;

//===Producer consumer buffer===
pthread_t worker_threads[MAX_SESSION_COUNT];
unsigned int thread_args[MAX_SESSION_COUNT];
setup_request buffer[BUFFER_SIZE];
int in = 0;
int out = 0;
pthread_mutex_t buffer_mutex;
pthread_cond_t buffer_not_full;
pthread_cond_t buffer_not_empty;



int main(int argc, char* argv[]) {
  int ret = parse_args(argc, argv);
  if (ret) return ret;

  ret = init_server();
  if (ret) return ret;

  //Main execution loop
  while (!server_should_quit) {
    accept_client();

    //Print requested data if flag is set
    if (show_flag) {
      //Reset flag (not using mutexes, not important given the use case)
      show_flag = 0;

      //Always print messge to acknowledge signal, even if event listing fails
      write(1, "Listing all events:\n", 20);

      //Get list of all events
      size_t count;
      unsigned int* data = ems_list_events_to_client(&count);

      //Ignore if error
      if (data == NULL) continue;

      //Actually do the printing
      for (size_t i = 0; i < count; i++) {
        char buff[32];
        snprintf(buff, 32, "Event %d:\n", data[i]);
        write(1, buff, strlen(buff));
        ems_show(1, data[i]);
      }
    }
  }

  close_server();
}

void* worker_thread_main(void* arg) {
  //Get session id (=thread id) from arg
  unsigned int session_id = *((unsigned int*)arg);

  //Block SIGUSR1
  sigset_t sigset;
  sigemptyset(&sigset);
  sigaddset(&sigset, SIGUSR1);
  pthread_sigmask(SIG_BLOCK, &sigset, NULL);

  //Work loop
  while (1) {
    //Fetch request to processs
    setup_request request = buffer_get();
    
    //Open provided pipes
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


//===Server startup===
int parse_args(int argc, char* argv[]) {
  //Error if invalid arguments
  if (argc < 2 || argc > 3) {
    fprintf(stderr, "Usage: %s\n <pipe_path> [delay]\n", argv[0]);
    return 1;
  }

  //Parse access_delay
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

  //Process pipe path
  if (argc >= 2) {
    FIFO_path = argv[1];
  }

  return 0;
}

int init_server() {
  //[Delete and] create and open request pipe
  unlink(FIFO_path);
  mkfifo(FIFO_path, FIFO_PERMS);
  registerFIFO = open(FIFO_path, O_RDWR);

  //Initialize synchronization methods for producer-consumer buffer
  pthread_mutex_init(&buffer_mutex, NULL);
  pthread_cond_init(&buffer_not_full, NULL);
  pthread_cond_init(&buffer_not_empty, NULL);

  //Launch worker threads
  for (int i = 0; i < MAX_SESSION_COUNT; i++) {
    thread_args[i] = (unsigned int)i;
    pthread_create(&worker_threads[i], NULL, worker_thread_main, &thread_args[i]);
  }

  //Initialize EMS
  if (ems_init(state_access_delay_us)) {
    fprintf(stderr, "Failed to initialize EMS\n");
    return 1;
  }

  //Register signal handlers
  signal(SIGUSR1, handle_SIGUSR1);
  signal(SIGINT, handle_SIGINT);

  //Set main loop condition
  server_should_quit = 0;

  return 0;
}


//===Client handling===
void accept_client() {
  //Read opcode (should be =1)
  char opcode;
  if (read(registerFIFO, &opcode, sizeof(char)) == -1) {
    if (errno == 4) { return; }  // ignore error if "Interrupted system call"
    fprintf(stderr, "Error reading from pipe while accepting client: %d.\n", errno);
    exit(1);
  }

  //Error on invalid setup opcode
  if (opcode != MSG_SETUP)
  {
    fprintf(stderr, "Invalid setup opcode %d. Expected %d.\n", opcode, MSG_SETUP);
    exit(1);
  }

  //Read setup data (pipe names)
  setup_request request;
  if (read(registerFIFO, &request, sizeof(setup_request)) == -1) {
    fprintf(stderr, "Error reading from pipe while accepting client paths: %d.\n", errno);
    exit(1);
  }

  //Add to producer-consumer buffer for worker threads to handle
  buffer_add(request);
}

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


//===Command processing and handling===
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


//===Server shutdown===
void close_server_threads() {
  for (int i = 0; i < MAX_SESSION_COUNT; i++) {
    pthread_cancel(worker_threads[i]);
  }
}

void close_server() {
  if (close(registerFIFO) == -1) {
    fprintf(stderr, "Error closing register FIFO\n");
    exit(1);
  }
  if (unlink(FIFO_path) == -1) {
    fprintf(stderr, "Error deleting register FIFO\n");
    exit(1);
  }
  close_server_threads();
  pthread_mutex_destroy(&buffer_mutex);
  pthread_cond_destroy(&buffer_not_full);
  pthread_cond_destroy(&buffer_not_empty);
  ems_terminate();
  exit(0);
}


//===Signal handling===
void handle_SIGUSR1(int signum) {
  (void)signum;
  show_flag = 1;

  signal(SIGUSR1, handle_SIGUSR1);
}

void handle_SIGINT(int signum)
{
  (void)signum;
  server_should_quit = 1;

  //Not needed: signal(SIGINT, handle_SIGINT);
  //Also helpful in case exit code fails and an actual Ctrl+C is needed
}



setup_request buffer_get()
{
  //Lock buffer
  pthread_mutex_lock(&buffer_mutex);
  //Wait for required condition
  while (in == out) pthread_cond_wait(&buffer_not_empty, &buffer_mutex);
  //Fetch from buffer and increment tail
  setup_request ret = buffer[out];
  out = (out + 1) % BUFFER_SIZE;
  //Signal that buffer is not full
  pthread_cond_signal(&buffer_not_full);
  //Unlock buffer
  pthread_mutex_unlock(&buffer_mutex);

  return ret;
}

void buffer_add(setup_request request)
{
  //Lock buffer
  pthread_mutex_lock(&buffer_mutex);
  //Wait for buffer to not be full
  while ((in + 1) % BUFFER_SIZE == out) pthread_cond_wait(&buffer_not_full, &buffer_mutex);
  //Add request to buffer
  buffer[in] = request;
  in = (in + 1) % BUFFER_SIZE;
  //Signal that buffer is not empty
  pthread_cond_signal(&buffer_not_empty);
  //Unlock buffer
  pthread_mutex_unlock(&buffer_mutex);
}
