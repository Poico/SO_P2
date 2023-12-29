#ifndef MESSAGES_H
#define MESSAGES_H

#include <sys/types.h>

enum MSG_OPCODE
{
	MSG_SETUP = 1,
	MSG_QUIT = 2,
	MSG_CREATE  = 3,
	MSG_RESERVE = 4,
	MSG_SHOW = 5,
	MSG_LIST = 6
};

typedef struct {
	char opcode;
	char request_fifo_name[40];
	char response_fifo_name[40];
} __attribute__((packed)) setup_request;

typedef struct {
	int session_id;
} __attribute__((packed)) setup_response;

typedef struct {
	char opcode;
} __attribute__((packed)) quit_request;

//NOT NEEDED: quit_response

typedef struct {
	char opcode;
	unsigned int event_id;
	size_t num_rows;
	size_t num_cols;
} __attribute__((packed)) create_request;

typedef struct {
	int return_code;
} __attribute__((packed)) create_response;

typedef struct {
	char opcode;
	unsigned int event_id;
	size_t num_seats;
} __attribute__((packed)) reserve_request;

typedef struct {
	int return_code;
} __attribute__((packed)) reserve_response;

typedef struct {
	char opcode;
	unsigned int event_id;
} __attribute__((packed)) show_request;

typedef struct {
	int return_code;
	size_t num_rows;
	size_t num_cols;
} __attribute__((packed)) show_response;

typedef struct {
	char opcode;
} __attribute__((packed)) list_request;

typedef struct {
	int return_code;
	size_t num_events;
} __attribute__((packed)) list_response;

#endif
