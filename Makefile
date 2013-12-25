CC ?= cc

CFLAGS += -Wno-pointer-sign -Wno-logical-op-parentheses

all: l9fb

l9fb: main.c 9p.c video.h video/fb.c util.h util.c
	${CC} ${CFLAGS} -o $@ main.c 9p.c video/fb.c util.c ${LIBS}
