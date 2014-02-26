#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <linux/mman.h>
#include <linux/fb.h>
#include <stdlib.h>

static int fbfd;
struct fb_fix_screeninfo fbfi;
struct fb_var_screeninfo fbvi;
void *fb;

int getVi () {
	size_t fbSize = fbfi.line_length * fbvi.yres;
	if (ioctl (fbfd, FBIOGET_VSCREENINFO, &fbvi) < 0) return (-1);
	fb = mremap (fb, fbSize, fbfi.line_length * fbvi.yres, MREMAP_MAYMOVE);
	if (fb == MAP_FAILED) return (-1);
	return 0;
}

int putVi () {
	size_t fbSize = fbfi.line_length * fbvi.yres;
	if (ioctl (fbfd, FBIOPUT_VSCREENINFO, &fbvi) < 0) return (-1);
	fb = mremap (fb, fbSize, fbfi.line_length * fbvi.yres, MREMAP_MAYMOVE);
	if (fb == MAP_FAILED) return (-1);
	return 0;
}

int initVideo () {
	char *path;
	(path = getenv ("FRAMEBUFFER")) || (path = "/dev/fb0");
	fbfd = open (path, O_RDWR);
	if (fbfd < 0) return (-1);
	if (ioctl (fbfd, FBIOGET_FSCREENINFO, &fbfi) < 0 ||
	    ioctl (fbfd, FBIOGET_VSCREENINFO, &fbvi) < 0) {
		close (fbfd);
		return (-1);
	}
	fb = mmap (0, fbfi.line_length * fbvi.yres, PROT_READ | PROT_WRITE, MAP_SHARED, fbfd, 0);
	if (fb == MAP_FAILED) return (-1);
	return 0;
}

void exitVideo () {
	close (fbfd);
	munmap (fb, fbfi.line_length * fbvi.yres);
}
