#include <unistd.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "9p.h"
#include "util.h"

ssize_t read9pmsg (int fd, uint8_t *msg, size_t n) {
	ssize_t m, size;
	
	if (n < 7) return (-1);
	m = read (fd, msg, 7);
	if (m == 0) return 0;
	if (m <  0) return (-1);
	LOAD32LE(size, msg);
	if (size < 7) return (-1);
	switch (msg[4] /* type */) {
	// Special case for TWrite and RRead, lest we copy huge data
	case RRead:
		if (n < 11 || read (fd, msg + 7, 4) < 4) return (-1);
		return 11;
	case TWrite:
		if (n < 24 || read (fd, msg + 7, 16) < 16) return (-1);
		return 23;
	default:
		if (n < size) return (-1);
		if (read (fd, msg + 7, size - 7) + 7 < size) return (-1);
		return size;
	}
}

ssize_t write9pmsg (int fd, uint8_t *msg) {
	size_t size;
	
	LOAD32LE(size, msg);
	switch (msg[4] /* type */) {
	case RRead:
		if (write (fd, msg, 11) < 11) return (-1);
		return 11;
	case TWrite:
		if (write (fd, msg, 23) < 23) return (-1);
		return 23;
	default:
		if (write (fd, msg, size) < size) return (-1);
		return size;
	}
}

size_t loadQid (Qid *p_qid, uint8_t *msg) {
	p_qid -> type = msg[0];
	LOAD32LE(p_qid -> vers, msg + 1);
	LOAD64LE(p_qid -> path, msg + 5);
	return 13;
}

size_t storQid (Qid *p_qid, uint8_t *msg) {
	msg[0] = p_qid -> type;
	STOR32LE(p_qid -> vers, msg + 1);
	STOR64LE(p_qid -> path, msg + 5);
	return 13;
}

size_t vsscan9p1 (uint8_t *msg, char fmt, void *p) {
	size_t l;
	switch (fmt) {
	case '0':
		*(uint8_t *)p = msg[0];
		return 1;
	case '1':
		LOAD16LE(*(uint16_t *)p, msg);
		return 2;
	case '2':
		LOAD32LE(*(uint32_t *)p, msg);
		return 4;
	case '3':
		LOAD64LE(*(uint64_t *)p, msg);
		return 8;
	case 'd':
		return loadDir (p, msg);
	case 'q':
		return loadQid (p, msg);
	case 's':
		LOAD16LE(l, msg);
		*(char **)p = xstrndup (msg + 2, l);
		return (2 + l);
	}
}

size_t vsprint9p1 (uint8_t *msg, char fmt, void *p) {
	size_t l;
	char *xs;
	switch (fmt) {
	case '0':
		if (msg) msg[0] = *(uint8_t *)p;
		return 1;
	case '1':
		if (msg) STOR16LE(*(uint16_t *)p, msg);
		return 2;
	case '2':
		if (msg) STOR32LE(*(uint32_t *)p, msg);
		return 4;
	case '3':
		if (msg) STOR64LE(*(uint64_t *)p, msg);
		return 8;
	case 'd':
		return storDir (p, msg);
	case 'q':
		return (msg ? storQid (p, msg) : 13);
	case 's':
		xs = *(char **)p;
		l = xs ? strlen (xs) : 0;
		if (msg) {
			STOR16LE(l, msg);
			if (xs) memcpy (msg + 2, xs, l);
		}
		return (2 + l);
	}
}

#define atoffset(t, p, r) (*(t *)((uint8_t *)(p) + r))

uint8_t *vsscan9p (uint8_t *msg, char *fmt, void *p, size_t rs[]) {
	size_t n;
	uint8_t *q;
	for (; fmt[0]; fmt++, rs++) switch (fmt[0]) {
	case 'l':
		msg += 2;
		rs--;
		break;
	case 'n':
		LOAD16LE(n, msg);
		msg += 2;
		atoffset (uint16_t, p, rs[0]) = n;
		fmt++;
		rs++;
		q = xrealloc (0, sizeof (void *)*n);
		atoffset (void *, p, rs[0]) = q;
		for (size_t ii = 0; ii < n; ii++) {
			size_t m = vsscan9p1 (msg, fmt[0], q);
			msg += m;
			q += fmt[0] == 's' ? sizeof (void *) : m;
		}
		break;
	default:
		msg += vsscan9p1 (msg, fmt[0], (uint8_t *)p + rs[0]);
	}
	return msg;
}

uint8_t *vsprint9p (uint8_t *msg, char *fmt, void *p, size_t rs[]) {
	size_t n;
	uint8_t *q;
	int w = msg != 0;
	for (; fmt[0]; fmt++, rs++) switch (fmt[0]) {
	case 'l':
		n = vsprint9p (0, fmt + 1, p, rs);
		if (w) STOR16LE(n, msg);
		msg += 2;
		rs--;
		break;
	case 'n':
		n = atoffset (uint16_t, p, rs[0]);
		if (w) STOR16LE(n, msg);
		msg += 2;
		fmt++;
		rs++;
		q = atoffset (void *, p, rs[0]);
		for (size_t ii = 0; ii < n; ii++) {
			size_t m = vsprint9p1 (w ? msg : 0, fmt[0], q);
			msg += m;
			q += fmt[0] == 's' ? sizeof (void *) : m;
		}
		break;
	default:
		msg += vsprint9p1 (w ? msg : 0, fmt[0], (uint8_t *)p + rs[0]);
	}
	return msg;
}

void *fmts[][2] = {
	[TVersion]	= { "2s",	(size_t []){ offsetof (Fcall, msize), offsetof (Fcall, version) } },
	[RVersion]	= { "2s",	(size_t []){ offsetof (Fcall, msize), offsetof (Fcall, version) } },
	[TAuth]		= { "2ss",	(size_t []){ offsetof (Fcall, afid), offsetof (Fcall, uname), offsetof (Fcall, aname) } },
	[RAuth]		= { "q",	(size_t []){ offsetof (Fcall, aqid) } },
	[TError]	= { 0 }, // invalid
	[RError]	= { "s",	(size_t []){ offsetof (Fcall, ename) } },
	[TFlush]	= { "1",	(size_t []){ offsetof (Fcall, oldtag) } },
	[RFlush]	= { "",		(size_t []){} },
	[TAttach]	= { "22ss",	(size_t []){ offsetof (Fcall, fid), offsetof (Fcall, afid), offsetof (Fcall, uname), offsetof (Fcall, aname) } },
	[RAttach]	= { "q",	(size_t []){ offsetof (Fcall, qid) } },
	[TWalk]		= { "22ns",	(size_t []){ offsetof (Fcall, fid), offsetof (Fcall, newfid), offsetof (Fcall, nwname), offsetof (Fcall, wname) } },
	[RWalk]		= { "nq",	(size_t []){ offsetof (Fcall, nwqid), offsetof (Fcall, wqid) } },
	[TOpen]		= { "20",	(size_t []){ offsetof (Fcall, fid), offsetof (Fcall, mode) } },
	[ROpen]		= { "q2",	(size_t []){ offsetof (Fcall, qid), offsetof (Fcall, iounit) } },
	[TCreate]	= { "2s20",	(size_t []){ offsetof (Fcall, fid), offsetof (Fcall, name), offsetof (Fcall, perm), offsetof (Fcall, mode) } },
	[RCreate]	= { "q2",	(size_t []){ offsetof (Fcall, qid), offsetof (Fcall, iounit) } },
	[TRead]		= { "232",	(size_t []){ offsetof (Fcall, fid), offsetof (Fcall, offset), offsetof (Fcall, count) } },
	[RRead]		= { "2",	(size_t []){ offsetof (Fcall, count) } },
	[TWrite]	= { "232",	(size_t []){ offsetof (Fcall, fid), offsetof (Fcall, offset), offsetof (Fcall, count) } },
	[RWrite]	= { "2",	(size_t []){ offsetof (Fcall, count) } },
	[TClunk]	= { "2",	(size_t []){ offsetof (Fcall, fid) } },
	[RClunk]	= { "",		(size_t []){} },
	[TRemove]	= { "2",	(size_t []){ offsetof (Fcall, fid) } },
	[RRemove]	= { "",		(size_t []){} },
	[TStat]		= { "2",	(size_t []){ offsetof (Fcall, fid) } },
	[RStat]		= { "ld",	(size_t []){ offsetof (Fcall, st) } },
	[TWStat]	= { "2ld",	(size_t []){ offsetof (Fcall, fid), offsetof (Fcall, st) } },
	[RWStat]	= { "",		(size_t []){} },
};

ssize_t loadFcall (Fcall *p_fcall, uint8_t *msg) {
	if (msg[4] < P9PMIN || msg[4] > P9PMAX || !fmts[msg[4]][1]) return (-1);
	p_fcall -> type = msg[4];
	LOAD16LE(p_fcall -> tag, msg + 5);
	return (vsscan9p (msg + 7, fmts[p_fcall -> type][0], p_fcall, fmts[p_fcall -> type][1]) - msg);
}

ssize_t storFcall (Fcall *p_fcall, uint8_t *msg) {
	size_t size;
	size = vsprint9p (msg ? msg + 7 : 0, fmts[p_fcall -> type][0], p_fcall, fmts[p_fcall -> type][1]) - (msg ? msg : (uint8_t *)(-7));
	if (msg) {
		msg[4] = p_fcall -> type;
		STOR16LE(p_fcall -> tag, msg + 5);
		STOR32LE(size + (msg[4] == RRead ? p_fcall -> count : 0), msg);
	}
	return size;
}

static
size_t goDir (Dir *p_d, uint8_t *msg, int w) {
	size_t l = (w ? vsprint9p : vsscan9p) (msg ? msg + 2 : 0, "12q2223ssss", p_d, (size_t []){ offsetof (Dir, type), offsetof (Dir, dev), offsetof (Dir, qid), offsetof (Dir, mode), offsetof (Dir, atime), offsetof (Dir, mtime), offsetof (Dir, length), offsetof (Dir, name), offsetof (Dir, uid), offsetof (Dir, gid), offsetof (Dir, muid) }) - (msg ? msg + 2 : 0);
	if (msg) STOR16LE(l, msg);
	return l + 2;
}

size_t loadDir (Dir *p_d, uint8_t *msg) {
	return goDir (p_d, msg, 0);
}

size_t storDir (Dir *p_d, uint8_t *msg) {
	return goDir (p_d, msg, 1);
}
