#include <linux/fb.h>

extern struct fb_fix_screeninfo fbfi;
extern struct fb_var_screeninfo fbvi;
extern void *fb;

int getVi ();
int putVi ();

int  initVideo ();
void exitVideo ();
