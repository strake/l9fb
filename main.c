#define NOPLAN9DEFINES
#include <u.h>
#include <libc.h>
#include <fcall.h>
#include <unistd.h>
#include <sys/mman.h>
#include <linux/fb.h>
#include <stdlib.h>
#include <stdio.h>
#include <err.h>

#include "video.h"

#define MAX(x, y) ((x) > (y) ? (x) : (y))
#define MIN(x, y) ((x) < (y) ? (x) : (y))

void *xrealloc (void *p, size_t n) {
	p = realloc (p, n);
	if (!p) err (1, "failed to allocate memory");
	return p;
}

char *xstrdup (const char *s) {
	char *t;
	if (!s) return 0;
	t = strdup (s);
	if (!t) err (1, "failed to allocate memory");
	return t;
}

size_t xread (int fd, void *x, size_t n) {
	if (read (fd, x, n) < n) err (1, "failed to read");
	return n;
}

enum {
	QPathNIL = 0,
	QPath_root,
	QPath_img,
	QPath_fi,
	QPath_vi,
	QPathMAX
};

char *qpathNames[QPathMAX] = {
	[QPath_img]	= "img",
	[QPath_fi]	= "fi",
	[QPath_vi]	= "vi",
};

Qid qpathQids[QPathMAX] = {
	[QPath_root]	= { .type = QTDIR,  .vers = 0, .path = QPath_root },
	[QPath_img]	= { .type = QTFILE, .vers = 0, .path = QPath_img  },
	[QPath_fi]	= { .type = QTFILE, .vers = 0, .path = QPath_fi   },
	[QPath_vi]	= { .type = QTFILE, .vers = 0, .path = QPath_vi   },
};

uint32_t qpathModes[QPathMAX] = {
	[QPath_root]	= DMREAD | DMEXEC,
	[QPath_img]	= DMREAD | DMWRITE,
	[QPath_fi]	= DMREAD,
	[QPath_vi]	= DMREAD | DMWRITE,
};

ssize_t qpathLength (uint64_t path) {
	switch (path) {
	case QPath_img:
		getVi ();
		return fbfi.line_length * fbvi.yres;
	case QPath_fi:
		return sizeof (struct fb_fix_screeninfo);
	case QPath_vi:
		return sizeof (struct fb_var_screeninfo);
	default:
		return 0;
	}
}

/* if dir, list of child qpaths
 * else,   pointer to file data
 */
void *qpathPtrs[QPathMAX] = {
	[QPath_root]	= (uint64_t []){ QPath_img, QPath_fi, QPath_vi, 0 },
	[QPath_fi]	= &fbfi,
	[QPath_vi]	= &fbvi,
};

void pointbuf (Fcall *p_fcall, void *x, size_t n) {
	if (p_fcall -> offset > n) {
		p_fcall -> count = 0;
		p_fcall -> data  = 0;
		return;
	}
	p_fcall -> count = MIN(p_fcall -> count, n - p_fcall -> offset);
	p_fcall -> data  = (uint8_t *)x + p_fcall -> offset;
}

void writebuf (Fcall *p_fcall, void *x, size_t n) {
	if (p_fcall -> offset > n) {
		p_fcall -> count = 0;
		return;
	}
	p_fcall -> count = MIN(p_fcall -> count, n - p_fcall -> offset);
	memcpy (x, (uint8_t *)x + p_fcall -> offset, p_fcall -> count);
}

void dostat (Dir *dir) {
	uint64_t path = dir -> qid.path;
	*dir = (Dir){
		.qid = qpathQids[path],
		.mode = qpathModes[path] << 6 | qpathModes[path] << 3 | qpathModes[path],
		.atime = 0, .mtime = 0,
		.length = qpathLength (path),
		.name = xstrdup (qpathNames[path]),
		.uid  = xstrdup (""),
		.gid  = xstrdup (""),
		.muid = xstrdup (""),
	};
}

int main (int argc, char *argu[]) {
	uint64_t *qpaths;
	uint8_t *msg;
	ssize_t n, msize = 1 << 24;
	
	if (initVideo () < 0) err (1, "failed to initialize video");
	atexit (exitVideo);
	
	qpathPtrs[QPath_img] = fb;
	
	qpaths = mmap (0, (size_t)8 << 32, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE | MAP_NORESERVE, -1, 0);
	if (qpaths == MAP_FAILED) err (1, "failed to allocate memory");
	
	msg = xrealloc (0, msize);
	for (;;) {
		Fcall fcall;
		Dir dir;
		n = read9pmsg (0, msg, msize);
		if (n <  0) err (1, "failed to read");
		fprintf (stderr, "%d\t%d\n", n, fcall.type);
		if (n == 0) break;
		convM2S (msg, n, &fcall);
		switch (fcall.type++) {
#define ERR(x) do { fcall.type = Rerror; fcall.ename = (x); } while (0)
		case Tversion:
			fcall.msize = msize = MIN(msize, fcall.msize);
			if (msize < 24) {
				ERR("msize too small");
				break;
			}
			if (strncmp (fcall.version, "9P2000", 6) == 0) fcall.version[6] = 0;
			else fcall.version = "unknown";
			break;
		case Tauth:
			ERR("l9fb: authentication not required");
			break;
		case Tflush:
			break;
		case Tattach:
			fcall.qid = qpathQids[QPath_root];
			break;
		case Twalk:
			if (!qpaths[fcall.fid] || fcall.fid != fcall.newfid && qpaths[fcall.newfid]) {
				ERR("bad fid");
				break;
			}
			{
				int ii;
				uint64_t qpath = qpaths[fcall.fid], *theseQPaths;
				for (ii = 0; ii < fcall.nwname; ii++) {
					theseQPaths = qpathPtrs[qpath];
					size_t jj;
					if (qpathQids[qpath].type != QTDIR) {
						ERR("no such file");
						break;
					}
					for (jj = 0; theseQPaths[jj] && strcmp (qpathNames[theseQPaths[jj]], fcall.wname[ii]) != 0; jj++);
					if (!theseQPaths[jj]) {
						ERR("no such file");
						break;
					}
					qpath = theseQPaths[jj];
					fcall.wqid[ii] = qpathQids[qpath];
				}
				fcall.nwqid = ii;
			}
			break;
		case Topen:
			if (!qpaths[fcall.fid]) ERR("bad fid");
			else {
				int modes[4] = {
					[OREAD]		= DMREAD,
					[OWRITE]	= DMWRITE,
					[ORDWR]		= DMREAD | DMWRITE,
					[OEXEC]		= DMREAD | DMWRITE | DMEXEC,
				};
				if (qpathModes[qpaths[fcall.fid]] & modes[fcall.mode] == modes[fcall.mode]) fcall.qid = qpathQids[qpaths[fcall.fid]];
				else ERR("denied");
			}
			break;
		case Tread:
			if (!qpaths[fcall.fid]) ERR("bad fid");
			if (!(qpathModes[qpaths[fcall.fid]] & DMREAD)) {
				ERR("denied");
				break;
			}
			getVi ();
			qpathPtrs[QPath_img] = fb;
			if (qpathQids[qpaths[fcall.fid]].type == QTDIR) {
				uint64_t *theseQPaths = qpathPtrs[qpaths[fcall.fid]];
				uint8_t *p, *q;
				p = fcall.data = msg + 9;
				for (int ii = 0; theseQPaths[ii]; ii++) {
					dir.qid = qpathQids[theseQPaths[ii]];
					dostat (&dir);
					if (fcall.offset >= sizeD2M (&dir)) {
						fcall.offset -= sizeD2M (&dir);
						continue;
					}
					q = p + sizeD2M (&dir) - fcall.offset;
					if (q - msg > msize) break;
					memmove (p, p + fcall.offset, convD2M (&dir, p, msize - (p - msg)) - fcall.offset);
					fcall.offset = 0;
					p = q;
				}
				fcall.count = q - (uint8_t *)fcall.data;
			}
			else pointbuf (&fcall, qpathPtrs[qpaths[fcall.fid]], qpathLength (qpaths[fcall.fid]));
			break;
		case Twrite:
			if (!qpaths[fcall.fid]) {
				ERR("bad fid");
				break;
			}
			if (!(qpathModes[qpaths[fcall.fid]] & DMWRITE)) {
				ERR("denied");
				break;
			}
			getVi ();
			qpathPtrs[QPath_img] = fb;
			writebuf (&fcall, qpathPtrs[qpaths[fcall.fid]], qpathLength (qpaths[fcall.fid]));
			if (putVi () < 0) ERR("failed");
			break;
		case Tclunk:
			qpaths[fcall.fid] = 0;
			break;
		case Tstat:
			if (!qpaths[fcall.fid]) {
				ERR("bad fid");
				break;
			}
			dir.qid = qpathQids[qpaths[fcall.fid]];
			dostat (&dir);
			fcall.stat = msg + 13;
			fcall.nstat = convD2M (&dir, msg + 13, msize - 13);
			break;
		case Tcreate:
		case Tremove:
		case Twstat:
			ERR("denied");
			break;
		default:
			ERR ("bad message");
#undef ERR
		}
		write (1, msg, convS2M (&fcall, msg, msize));
	}
}
