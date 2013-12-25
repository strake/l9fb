#include <memory.h>
#include <unistd.h>
#include <sys/mman.h>
#include <linux/fb.h>
#include <stdlib.h>
#include <stdio.h>
#include <err.h>

#include "9p.h"
#include "video.h"
#include "util.h"

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

/* if dir, list of child qpaths
 * else,   pointer to file data
 */
void *qpathPtrs[QPathMAX] = {
	[QPath_root]	= (uint64_t []){ QPath_img, QPath_fi, QPath_vi, 0 },
	[QPath_fi]	= &fbfi,
	[QPath_vi]	= &fbvi,
};

void dostat (Dir *);

ssize_t qpathLength (uint64_t path) {
	if (qpathQids[path].type == QTDIR) {
		uint64_t *theseQPaths = qpathPtrs[path];
		size_t l = 0;
		for (int ii = 0; theseQPaths[ii]; ii++) {
			Dir dir = { .qid = qpathQids[theseQPaths[ii]] };
			dostat (&dir);
			l += storDir (&dir, 0);
		}
		return l;
	}
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

size_t clip (Fcall fcall, void *x, size_t n) {
	return (fcall.offset > n ? 0 : MIN(fcall.count, n - fcall.offset));
}

void dostat (Dir *p_dir) {
	uint64_t path = p_dir -> qid.path;
	*p_dir = (Dir){
		.qid = qpathQids[path],
		.mode = qpathQids[path].type << 24 | qpathModes[path] << 6 | qpathModes[path] << 3 | qpathModes[path],
		.atime = 0, .mtime = 0,
		.length = qpathLength (path),
		.name = xstrdup (qpathNames[path]),
		.uid  = 0,
		.gid  = 0,
		.muid = 0,
	};
}

int main (int argc, char *argu[]) {
	uint64_t *qpaths;
	uint8_t *msg;
	ssize_t n, msize = 1 << 24;
	void *x;
	
	if (initVideo () < 0) err (1, "failed to initialize video");
	atexit (exitVideo);
	
	qpathPtrs[QPath_img] = fb;
	
	qpaths = mmap (0, (size_t)8 << 32, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE | MAP_NORESERVE, -1, 0);
	if (qpaths == MAP_FAILED) err (1, "failed to allocate memory");
	
	msg = xrealloc (0, msize);
	for (;;) {
		Fcall fcall;
		n = read9pmsg (0, msg, msize);
		if (n <  0) err (1, "failed to read");
		if (n == 0) break;
		if (loadFcall (&fcall, msg) < 0) err (1, "bad message");
		switch (fcall.type++) {
#define ERR(x) do { fcall.type = RError; fcall.ename = (x); } while (0)
		case TVersion:
			fcall.msize = msize = MIN(msize, fcall.msize);
			if (msize < 24) {
				ERR("msize too small");
				break;
			}
			if (strncmp (fcall.version, "9P2000", 6) == 0) fcall.version[6] = 0;
			else fcall.version = "unknown";
			break;
		case TAuth:
			ERR("l9fb: authentication not required");
			break;
		case TFlush:
			break;
		case TAttach:
			qpaths[fcall.fid] = QPath_root;
			fcall.qid = qpathQids[QPath_root];
			break;
		case TWalk:
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
				if (ii == fcall.nwname) qpaths[fcall.newfid] = qpath;
			}
 			break;
		case TOpen:
			if (!qpaths[fcall.fid]) ERR("bad fid");
			else {
				int modes[4] = {
					[OREAD]		= DMREAD,
					[OWRITE]	= DMWRITE,
					[ORDWR]		= DMREAD | DMWRITE,
					[OEXEC]		= DMREAD | DMWRITE | DMEXEC,
				};
				if ((qpathModes[qpaths[fcall.fid]] & modes[fcall.mode & 3]) == modes[fcall.mode & 3]) fcall.qid = qpathQids[qpaths[fcall.fid]];
				else ERR("denied");
			}
			break;
		case TRead:
			if (!qpaths[fcall.fid]) ERR("bad fid");
			if (!(qpathModes[qpaths[fcall.fid]] & DMREAD)) {
				ERR("denied");
				break;
			}
			getVi ();
			qpathPtrs[QPath_img] = fb;
			if (qpathQids[qpaths[fcall.fid]].type == QTDIR) {
				uint64_t *theseQPaths = qpathPtrs[qpaths[fcall.fid]];
				uint8_t *p;
				p = x = msg + 11;
				for (int ii = 0; theseQPaths[ii]; ii++) {
					Dir dir;
					dir.qid = qpathQids[theseQPaths[ii]];
					dostat (&dir);
					if (fcall.offset >= storDir (&dir, 0)) {
						fcall.offset -= storDir (&dir, 0);
						continue;
					}
					if (storDir (&dir, 0) + 11 > msize) ERR("too big");
					memmove (p, p + fcall.offset, storDir (&dir, p) - fcall.offset);
					p += storDir (&dir, 0) - fcall.offset;
					fcall.offset = 0;
					if (p - msg - 11 > fcall.count) {
						p = msg + 11 + fcall.count;
						break;
					}
				}
				if (fcall.type != RError) fcall.count = p - msg - 11;
			}
			else {
				fcall.count = clip (fcall, qpathPtrs[qpaths[fcall.fid]], qpathLength (qpaths[fcall.fid]));
				x = (uint8_t *)(qpathPtrs[qpaths[fcall.fid]]) + fcall.offset;
			}
			break;
		case TWrite:
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
			{
				uint8_t _;
				size_t m = clip (fcall, qpathPtrs[qpaths[fcall.fid]], qpathLength (qpaths[fcall.fid]));
				xread (0, (uint8_t *)(qpathPtrs[qpaths[fcall.fid]]) + fcall.offset, m);
				for (; m < fcall.count; m++) read (0, &_, 1);
			}
			if (putVi () < 0) ERR("failed");
			break;
		case TClunk:
			qpaths[fcall.fid] = 0;
			break;
		case TStat:
			if (!qpaths[fcall.fid]) {
				ERR("bad fid");
				break;
			}
			fcall.st.qid = qpathQids[qpaths[fcall.fid]];
			dostat (&fcall.st);
			break;
		case TCreate:
		case TRemove:
			ERR("denied");
			break;
		case TWStat:
			// ignore; unusable otherwise
			break;
		default:
			ERR ("bad message");
#undef ERR
		}
		write (1, msg, storFcall (&fcall, msg));
		switch (fcall.type) {
		case RRead:
			if (write (1, x, fcall.count) < fcall.count) err (1, "failed to write");
			break;
		}
	}
}
