#define NOPLAN9DEFINES
#include <u.h>
#include <libc.h>
#include <fcall.h>
#include <thread.h>
#include <9p.h>
#include <unistd.h>
#include <linux/fb.h>
#include <stdlib.h>
#include <stdio.h>
#include <err.h>

#include "video.h"

#define MAX(x, y) ((x) > (y) ? (x) : (y))
#define MIN(x, y) ((x) < (y) ? (x) : (y))

char *xstrdup (const char *s) {
	char *t;
	if (!s) return 0;
	t = strdup (s);
	if (!t) err (1, "failed to allocate memory");
	return t;
}

enum {
	QPath_root = 0,
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
	[QPath_img]	= { .type = QTFILE, .vers = 0, .path = QPath_img },
	[QPath_fi]	= { .type = QTFILE, .vers = 0, .path = QPath_fi  },
	[QPath_vi]	= { .type = QTFILE, .vers = 0, .path = QPath_vi  },
};

uint32_t qpathModes[QPathMAX] = {
	[0]		= DMREAD | DMEXEC,
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

void l9fb_attach (Req *r) {
	r -> ofcall.qid = (Qid){ .type = QTDIR, .vers = 0, .path = 0 };
	r -> fid -> qid = r -> ofcall.qid;
	respond (r, 0);
}

char *l9fb_walk1 (Fid *fid, char *name, void *_) {
	size_t ii;
	
	switch (fid -> qid.path) {
	case 0: // root
		for (ii = 0; ii < QPathMAX; ii++) if (qpathNames[ii] && strcmp (qpathNames[ii], name) == 0) break;
		if (ii >= QPathMAX) return "No such file";
		fid -> qid = qpathQids[ii];
		return 0;
	default:
		return "No such file";
	}
}

void l9fb_open (Req *r) {
	respond (r, 0);
}

void l9fb_dostat (Dir *dir) {
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

int l9fb_genroot (int n, Dir *dir, void *_) {
	uint64_t paths[] = { QPath_img, QPath_fi, QPath_vi, 0 };
	if (!paths[n]) return -1;
	dir -> qid = qpathQids[paths[n]];
	l9fb_dostat (dir);
	return 0;
}

void l9fb_read (Req *r) {
	switch (r -> fid -> qid.path) {
	case 0:
		dirread9p (r, l9fb_genroot, 0);
		respond (r, 0);
		break;
	case QPath_img:
		getVi ();
		readbuf (r, fb, fbfi.line_length * fbvi.yres);
		respond (r, 0);
		break;
	case QPath_fi:
		readbuf (r, &fbfi, sizeof (struct fb_fix_screeninfo));
		respond (r, 0);
		break;
	case QPath_vi:
		if (getVi () < 0) {
			respond (r, "Failure");
			break;
		}
		readbuf (r, &fbvi, sizeof (struct fb_var_screeninfo));
		respond (r, 0);
		break;
	default:
		respond (r, "Bad qid");
	}
}

void l9fb_write (Req *r) {
	switch (r -> fid -> qid.path) {
	case QPath_img:
		getVi ();
		r -> ofcall.count = MIN(fbfi.line_length * fbvi.yres - r -> ifcall.offset, r -> ifcall.count);
		memcpy ((uint8_t *)fb + r -> ifcall.offset, r -> ifcall.data, r -> ofcall.count);
		respond (r, 0);
		break;
	case QPath_vi:
		if ((r -> ifcall.offset | r -> ifcall.count) & 3) {
			respond (r, "Misaligned write");
			break;
		}
		r -> ofcall.count = MIN(MAX (0, sizeof (struct fb_var_screeninfo) - r -> ifcall.offset), r -> ifcall.count);
		memcpy ((uint8_t *)(&fbvi) + r -> ifcall.offset, r -> ifcall.data, r -> ifcall.count);
		if (putVi () < 0) {
			respond (r, "Failure");
			break;
		}
		respond (r, 0);
		break;
	default:
		respond (r, "Bad qid");
	}
}

void l9fb_stat (Req *r) {
	r -> d.qid = r -> fid -> qid;
	l9fb_dostat (&(r -> d));
	respond (r, 0);
}

void l9fb_walk (Req *r) {
	walkandclone (r, l9fb_walk1, 0, 0);
}

Srv l9fb_srv = {
	.tree = 0,
	
	.attach	= l9fb_attach,
	.auth	= 0,
	.open	= l9fb_open,
	.create	= 0,
	.read	= l9fb_read,
	.write	= l9fb_write,
	.remove	= 0,
	.flush	= 0,
	.stat	= l9fb_stat,
	.wstat	= 0,
	.walk	= l9fb_walk,
	
	.walk1 = 0,
	.clone = 0,
	
	.destroyfid = 0,
	.destroyreq = 0,
	.start = 0,
	.end   = 0,
	.aux = 0,
	
	.infd  = 0,
	.outfd = 1,
	.nopipe = 1,
};

int main (int argc, char *argu[]) {
	if (initVideo () < 0) err (1, "failed to initialize video");
	atexit (exitVideo);
	srv (&l9fb_srv);
}
