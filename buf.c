/*
 * buf.c - Circular buffer, part of spcplayer
 * Copyright (C) 2011 Benjamin Charron <bcharron@pobox.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * buf.c - Benjamin Charron <bcharron@pobox.com>
 * Created  : Mon  8 Jun 2015 19:34:01 EDT
 * Revision : $Id$
 */

#include <assert.h>
#include "buf.h"

/* Creates a buffer of 'size' bytes */
buf_t *buffer_create(int size) {
	buf_t *buf;

	buf = malloc(sizeof(buf_t));
	if (NULL == buf) {
		perror("create_buffer().malloc(buf)");
		exit(1);
	}

	buf->data = malloc(size);
	if (NULL == buf->data) {
		perror("create_buffer().malloc(buf->data)");
		exit(1);
	}

	buf->size = size;
	buf->len = 0;
	buf->head = 0;
	buf->tail = 0;

	return(buf);
}

/* Returns 1 if the buffer is full, 0 otherwise. */
int buffer_is_full(buf_t *buf) {
	return((buf->size - buf->len) == 0);
}

/* Add a byte to the buffer. Returns 1 on success, 0 if there was no room */
int buffer_add_one(buf_t *buf, Sint16 sample) {
	int ret;

	if (! buffer_is_full(buf)) {
		printf("Adding to position tail = %d\n", buf->tail);
		buf->data[buf->tail++] = sample;
		buf->tail %= buf->size;
		buf->len++;
		ret = 1;
	} else {
		ret = 0;
	}

	return(ret);
}

/* Get the number of free bytes in the buffer */
int buffer_get_free(buf_t *buf) {
	return(buf->size - buf->len);
}

/* 
 * Read one byte from the buffer. It is an error to try to read from an empty
 * buffer
 */
Sint16 buffer_get_one(buf_t *buf) {
	Sint16 val;

	assert(buffer_get_len(buf) > 0);

	printf("head: %d\n", buf->head);
	val = buf->data[buf->head++];
	buf->head %= buf->size;
	buf->len--;

	return(val);
}

/* Get the number of bytes held in the buffer */
int buffer_get_len(buf_t *buf) {
	return(buf->len);
}

/* Free the memory of the buffer. Might be useful to take **buf in order to set
 * it NULL */
void buffer_release(buf_t *buf) {
	free(buf->data);
	buf->data = NULL;
	free(buf);
}

