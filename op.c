#include <stdint.h>
#include <stdlib.h>
#include <memory.h>
#include <string.h>

#include "9p.h"
#include "video.h"
#include "util.h"

typedef struct {
	char *name;
	char *(*f) (uint8_t *, size_t, void *);
	void *x;
} Op;

char *fill (uint8_t *msg, size_t l) {
	uint32_t xl, yl, xu, yu, bytesPerPixel = fbvi.bits_per_pixel + 7 >> 3;
	
	if (l < 16 + bytesPerPixel) return "bad message";
	
	LOAD32LE(xl, msg);
	LOAD32LE(yl, msg + 4);
	LOAD32LE(xu, msg + 8);
	LOAD32LE(yu, msg + 12);
	
	if (xl > xu) SWAP(xl, xu);
	if (yl > yu) SWAP(yl, yu);
	
	xl = MIN(xl, fbvi.xres - 1);
	xu = MIN(xu, fbvi.xres - 1);
	yl = MIN(yl, fbvi.yres - 1);
	yu = MIN(yu, fbvi.yres - 1);
	
	for (uint32_t ii = xl; ii <= xu; ii++) for (uint32_t jj = yl; jj <= yu; jj++) memcpy ((uint8_t *)fb + fbfi.line_length*jj + bytesPerPixel*ii, msg + 16, bytesPerPixel);
	
	return 0;
}

Op ops[] = {
	{ .name = "fill", .f = fill },
	{ 0 }
};

Fcall opOp (Fcall fcall) {
	uint8_t *msg;
	char *err;
	Op *p_op;
	size_t l;
	msg = xrealloc (0, fcall.count);
	xread (0, msg, fcall.count);
	for (int ii = 0; ops[ii].name; ii++) if (l = strlen (ops[ii].name), fcall.count >= l + 1 && memcmp (ops[ii].name, msg, l) == 0 && msg[l] == ' ') {
		err = ops[ii].f (msg + l + 1, fcall.count - l - 1, ops[ii].x);
		free (msg);
		if (err) {
			fcall.type = RError;
			fcall.ename = err;
		}
		return fcall;
	}
	free (msg);
	fcall.type = RError;
	fcall.ename = "bad op";
	return fcall;
};
