#ifndef _BUF_H
#define _BUF_H

// For Sint16
#include <SDL.h>

/* A circular buffer */
typedef struct buffer_s {
	int size;	// How many bytes are allocated for the buffer
	int len;	// How many bytes are currently used the buffer
	int head;	// Position of the first byte in the buffer
	int tail;	// Position of the last byte in the buffer
	Sint16 *data;
} buf_t;

buf_t *buffer_create(int size);
int buffer_is_full(buf_t *buf);
int buffer_add_one(buf_t *buf, Sint16 sample);
Sint16 buffer_get_one(buf_t *buf);
int buffer_get_len(buf_t *buf);
void buffer_release(buf_t *buf);
int buffer_get_free(buf_t *buf);
#endif
