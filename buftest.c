#include <assert.h>
#include "buf.h"

int main(int argc, char *argv[]) {
	int x;
	buf_t *buf;
	Sint16 v;
	int ret;
	int loop;

	buf = buffer_create(100);
	assert(NULL != buf);

	for (loop = 0; loop < 5; loop++) {
		/* All these adds should succeed */
		for (x = 1; x <= 100; x++) {
			v = (Sint16) x;
			printf("buffer_add_one(buf, %d)\n", v);
			ret = buffer_add_one(buf, v);

			assert(ret == 1);
			assert(buffer_get_len(buf) == x);
			assert(buffer_get_free(buf) == (100 - x));
		}

		assert(buffer_get_len(buf) == 100);

		/* This add should fail - buffer is full */
		ret = buffer_add_one(buf, 0);
		assert(ret == 0);

		for (x = 1; x <= 100; x++) {
			v = buffer_get_one(buf);
			printf("get[%d]: %d\n", x, v);
			assert(v == x);
			assert(buffer_get_len(buf) == (100 - x));
		}

		x = buffer_get_len(buf);
		printf("buffer len: %d\n", x);
		assert(x == 0);
	}

	return(0);
}
