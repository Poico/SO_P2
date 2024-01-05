#ifndef MESSAGES_H
#define MESSAGES_H

#include <sys/types.h>

// Enumeration of message opcodes
enum MSG_OPCODE
{
	MSG_SETUP = 1,    // Opcode for setup message
	MSG_QUIT = 2,     // Opcode for quit message
	MSG_CREATE  = 3,  // Opcode for create message
	MSG_RESERVE = 4,  // Opcode for reserve message
	MSG_SHOW = 5,     // Opcode for show message
	MSG_LIST = 6      // Opcode for list message
};

// Structure for core request message
typedef struct {
	char opcode;             // Opcode of the request
	unsigned int session_id; // Session ID
} __attribute__((packed)) core_request;

// Structure for setup request message
typedef struct {
	char request_fifo_name[40];   // Name of the request FIFO
	char response_fifo_name[40];  // Name of the response FIFO
} __attribute__((packed)) setup_request;

// Structure for setup response message
typedef struct {
	unsigned int session_id;  // Session ID
} __attribute__((packed)) setup_response;

// Structure for create request message
typedef struct {
	unsigned int event_id;  // Event ID
	size_t num_rows;        // Number of rows
	size_t num_cols;        // Number of columns
} __attribute__((packed)) create_request;

// Structure for create response message
typedef struct {
	int return_code;  // Return code
} __attribute__((packed)) create_response;

// Structure for reserve request message
typedef struct {
	unsigned int event_id;  // Event ID
	size_t num_seats;       // Number of seats to reserve
} __attribute__((packed)) reserve_request;

// Structure for reserve response message
typedef struct {
	int return_code;  // Return code
} __attribute__((packed)) reserve_response;

// Structure for show request message
typedef struct {
	unsigned int event_id;  // Event ID
} __attribute__((packed)) show_request;

// Structure for show response message
typedef struct {
	int return_code;   // Return code
	size_t num_rows;   // Number of rows
	size_t num_cols;   // Number of columns
} __attribute__((packed)) show_response;

// Structure for list response message
typedef struct {
	int return_code;    // Return code
	size_t num_events;  // Number of events
} __attribute__((packed)) list_response;

#endif
