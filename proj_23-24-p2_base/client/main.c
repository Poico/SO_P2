#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "api.h"
#include "common/constants.h"
#include "parser.h"
/**
 * The main function of the client program.
 * It takes command line arguments and performs various operations based on the commands received.
 * The program communicates with a server using named pipes.
 * 
 * @param argc The number of command line arguments.
 * @param argv An array of strings containing the command line arguments.
 *             The expected arguments are:
 *               - <request pipe path>: The path to the named pipe used for sending requests to the server.
 *               - <response pipe path>: The path to the named pipe used for receiving responses from the server.
 *               - <server pipe path>: The path to the named pipe used for communicating with the server.
 *               - <.jobs file path>: The path to the input file containing commands to be executed.
 * 
 * @return 0 if the program executed successfully, 1 otherwise.
 */
int main(int argc, char* argv[]) {
  // Check if the required number of command line arguments is provided
  if (argc < 5) {
    fprintf(stderr, "Usage: %s <request pipe path> <response pipe path> <server pipe path> <.jobs file path>\n",
            argv[0]);
    return 1;
  }

  // Set up the Event Management System (EMS) by connecting to the server
  if (ems_setup(argv[1], argv[2], argv[3])) {
    fprintf(stderr, "Failed to set up EMS\n");
    return 1;
  }

  // Validate the provided .jobs file path
  const char* dot = strrchr(argv[4], '.');
  if (dot == NULL || dot == argv[4] || strlen(dot) != 5 || strcmp(dot, ".jobs") ||
      strlen(argv[4]) > MAX_JOB_FILE_NAME_SIZE) {
    fprintf(stderr, "The provided .jobs file path is not valid. Path: %s\n", argv[1]);
    return 1;
  }

  // Generate the output file path by replacing the extension of the input file with ".out"
  char out_path[MAX_JOB_FILE_NAME_SIZE];
  strcpy(out_path, argv[4]);
  strcpy(strrchr(out_path, '.'), ".out");

  // Open the input file for reading
  int in_fd = open(argv[4], O_RDONLY);
  if (in_fd == -1) {
    fprintf(stderr, "Failed to open input file. Path: %s\n", argv[4]);
    return 1;
  }

  // Open the output file for writing
  int out_fd = open(out_path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
  if (out_fd == -1) {
    fprintf(stderr, "Failed to open output file. Path: %s\n", out_path);
    return 1;
  }

  // Process commands from the input file until the end of file is reached
  while (1) {
    unsigned int event_id;
    size_t num_rows, num_columns, num_coords;
    unsigned int delay = 0;
    size_t xs[MAX_RESERVATION_SIZE], ys[MAX_RESERVATION_SIZE];

    // Get the next command from the input file
    switch (get_next(in_fd)) {
      case CMD_CREATE:
        // Parse the CREATE command and execute it
        if (parse_create(in_fd, &event_id, &num_rows, &num_columns) != 0) {
          fprintf(stderr, "Invalid command. See HELP for usage\n");
          continue;
        }

        if (ems_create(event_id, num_rows, num_columns)) fprintf(stderr, "Failed to create event\n");
        break;

      case CMD_RESERVE:
        // Parse the RESERVE command and execute it
        num_coords = parse_reserve(in_fd, MAX_RESERVATION_SIZE, &event_id, xs, ys);

        if (num_coords == 0) {
          fprintf(stderr, "Invalid command. See HELP for usage\n");
          continue;
        }

        if (ems_reserve(event_id, num_coords, xs, ys)) fprintf(stderr, "Failed to reserve seats\n");
        break;

      case CMD_SHOW:
        // Parse the SHOW command and execute it
        if (parse_show(in_fd, &event_id) != 0) {
          fprintf(stderr, "Invalid command. See HELP for usage\n");
          continue;
        }

        if (ems_show(out_fd, event_id)) fprintf(stderr, "Failed to show event\n");
        break;

      case CMD_LIST_EVENTS:
        // Execute the LIST command
        if (ems_list_events(out_fd)) fprintf(stderr, "Failed to list events\n");
        break;

      case CMD_WAIT:
        // Parse the WAIT command and execute it
        if (parse_wait(in_fd, &delay, NULL) == -1) {
            fprintf(stderr, "Invalid command. See HELP for usage\n");
            continue;
        }

        if (delay > 0) {
            printf("Waiting...\n");
            sleep(delay);
        }
        break;

      case CMD_INVALID:
        fprintf(stderr, "Invalid command. See HELP for usage\n");
        break;

      case CMD_HELP:
        // Print the available commands
        printf(
            "Available commands:\n"
            "  CREATE <event_id> <num_rows> <num_columns>\n"
            "  RESERVE <event_id> [(<x1>,<y1>) (<x2>,<y2>) ...]\n"
            "  SHOW <event_id>\n"
            "  LIST\n"
            "  WAIT <delay_ms>\n"
            "  HELP\n");

        break;

      case CMD_EMPTY:
        break;

      case EOC:
        // Close the input and output files, and quit the EMS
        close(in_fd);
        close(out_fd);
        ems_quit();
        return 0;
    }
  }
}
