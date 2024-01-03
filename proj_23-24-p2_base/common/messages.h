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

typedef struct{
	MSG_OPCODE opcode;
	int session_id;
	union {
		struct {
			char request_fifo_name[40];
			char response_fifo_name[40];
		} setup;
		struct {
			unsigned int event_id;
			size_t num_rows;
			size_t num_cols;
		} create;
		struct {
			unsigned int event_id;
			size_t num_seats;
		} reserve;
		struct {
			unsigned int event_id;
		} show;
		struct {
			// Nothing
		} list;
	};
} __attribute__((packed)) request;

typedef struct{
	MSG_OPCODE opcode;
	union {
		struct {
			int session_id;
		} setup;
		struct {
			int return_code;
		} create;
		struct {
			int return_code;
		} reserve;
		struct {
			int return_code;
			size_t num_rows;
			size_t num_cols;
		} show;
		struct {
			int return_code;
			size_t num_events;
		} list;
	};
} __attribute__((packed)) response;

#endif
