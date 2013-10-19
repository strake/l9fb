#define NOPLAN9DEFINES
#include <u.h>
#include <libc.h>
#include <fcall.h>
#include <thread.h>
#include <9p.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <linux/fb.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <err.h>

char *xstrdup (const char *s) {
	char *t;
	if (!s) return 0;
	t = strdup (s);
	if (!t) err (1, "failed to allocate memory");
	return t;
}

struct fb_fix_screeninfo fbfi;
struct fb_var_screeninfo fbvi;
void *fb;

enum {
	QPath_root = 0,
	QPath_img,
	QPathMAX
};

char *qpathNames[QPathMAX] = {
	[QPath_img]	= "img",
};

Qid qpathQids[QPathMAX] = {
	[QPath_img]	= { .type = QTFILE, .vers = 0, .path = QPath_img },
};

uint32_t qpathModes[QPathMAX] = {
	[0]		= DMREAD | DMEXEC,
	[QPath_img]	= DMREAD | DMWRITE,
};

ssize_t qpathLength (uint64_t path) {
	switch (path) {
	case QPath_img:
		return fbfi.line_length * fbvi.yres;
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
		.mode = qpathModes[path],
		.atime = 0, .mtime = 0,
		.length = qpathLength (path),
		.name = xstrdup (qpathNames[path]),
		.uid  = xstrdup (""),
		.gid  = xstrdup (""),
		.muid = xstrdup (""),
	};
}

int l9fb_genroot (int n, Dir *dir, void *_) {
	uint64_t paths[] = { QPath_img, 0 };
	if (!paths[n]) return -1;
	dir -> qid.path = paths[n];
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
		readbuf (r, fb, fbfi.line_length * fbvi.yres);
		respond (r, 0);
		break;
	default:
		respond (r, "Bad qid");
	}
}

void l9fb_write (Req *r) {
	size_t fbSize = fbfi.line_length * fbvi.yres;
	switch (r -> fid -> qid.path) {
	case QPath_img:
		r -> ofcall.count = r -> ifcall.offset + r -> ofcall.count > fbSize ? fbSize - r -> ifcall.offset : r -> ifcall.count;
		memcpy ((uint8_t *)fb + r -> ifcall.offset, r -> ifcall.data, r -> ofcall.count);
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
	{
		char *path;
		int fd;
		
		(path = getenv ("FRAMEBUFFER")) || (path = "/dev/fb0");
		fd = open (path, O_RDWR);
		if (fd < 0) err (1, "failed to open framebuffer %s", path);
		if (ioctl (fd, FBIOGET_FSCREENINFO, &fbfi) < 0 ||
		    ioctl (fd, FBIOGET_VSCREENINFO, &fbvi) < 0) err (1, "failed to learn framebuffer size");
		fb = mmap (0, fbfi.line_length * fbvi.yres, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
		if (!fb) err (1, "failed to mmap framebuffer %s", path);
		close (fd);
	}

	srv (&l9fb_srv);

	munmap (fb, fbfi.line_length * fbvi.yres);
}
