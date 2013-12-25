/* Copyright ©2006-2010 Kris Maglione <maglione.k at Gmail>
 * See LICENSE file for license details.
 */

#include <unistd.h>
#include <stdint.h>

/* 9P message types */
enum {
	P9PMIN		= 0x64,
	TVersion	= 0x64,
	RVersion,
	TAuth		= 0x66,
	RAuth,
	TAttach		= 0x68,
	RAttach,
	TError		= 0x6A, /* illegal */
	RError,
	TFlush		= 0x6C,
	RFlush,
	TWalk		= 0x6E,
	RWalk,
	TOpen		= 0x70,
	ROpen,
	TCreate		= 0x72,
	RCreate,
	TRead		= 0x74,
	RRead,
	TWrite		= 0x76,
	RWrite,
	TClunk		= 0x78,
	RClunk,
	TRemove		= 0x7A,
	RRemove,
	TStat		= 0x7C,
	RStat,
	TWStat		= 0x7E,
	RWStat,
	P9PMAX		= 0x7F,
};

/* from libc.h */
enum {
	OREAD		= 0x0000,	/* open for read */
	OWRITE		= 0x0001,	/* write */
	ORDWR		= 0x0002,	/* read and write */
	OEXEC		= 0x0003,	/* execute, == read but check execute permission */
	OTRUNC		= 0x0010,	/* or'ed in (except for exec), truncate file first */
	OCEXEC		= 0x0020,	/* or'ed in, close on exec */
	ORCLOSE		= 0x0040,	/* or'ed in, remove on close */
	ODIRECT		= 0x0080,	/* or'ed in, direct access */
	ONONBLOCK	= 0x0100,	/* or'ed in, non-blocking call */
	OEXCL		= 0x1000,	/* or'ed in, exclusive use (create only) */
	OLOCK		= 0x2000,	/* or'ed in, lock after opening */
	OAPPEND		= 0x4000	/* or'ed in, append only */
};

/* bits in Qid.type */
enum {
	QTDIR		= 0x80,	/* type bit for directories */
	QTAPPEND	= 0x40,	/* type bit for append only files */
	QTEXCL		= 0x20,	/* type bit for exclusive use files */
	QTMOUNT		= 0x10,	/* type bit for mounted channel */
	QTAUTH		= 0x08,	/* type bit for authentication file */
	QTTMP		= 0x04,	/* type bit for non-backed-up file */
	QTSYMLINK	= 0x02,	/* type bit for symbolic link */
	QTFILE		= 0x00	/* type bits for plain file */
};

/* bits in Stat.mode */
enum {
	DMEXEC	= 0x1,		/* mode bit for execute permission */
	DMWRITE	= 0x2,		/* mode bit for write permission */
	DMREAD	= 0x4,		/* mode bit for read permission */

	DMDIR		= 0x80000000,	/* mode bit for directories */
	DMAPPEND	= 0x40000000,	/* mode bit for append only files */
	DMEXCL		= 0x20000000,	/* mode bit for exclusive use files */
	DMMOUNT		= 0x10000000,	/* mode bit for mounted channel */
	DMAUTH		= 0x08000000,	/* mode bit for authentication file */
	DMTMP		= 0x04000000,	/* mode bit for non-backed-up file */
	DMSYMLINK	= 0x02000000,	/* mode bit for symbolic link (Unix, 9P2000.u) */
	DMDEVICE	= 0x00800000,	/* mode bit for device file (Unix, 9P2000.u) */
	DMNAMEDPIPE	= 0x00200000,	/* mode bit for named pipe (Unix, 9P2000.u) */
	DMSOCKET	= 0x00100000,	/* mode bit for socket (Unix, 9P2000.u) */
	DMSETUID	= 0x00080000,	/* mode bit for setuid (Unix, 9P2000.u) */
	DMSETGID	= 0x00040000,	/* mode bit for setgid (Unix, 9P2000.u) */
};


typedef struct {
	uint8_t		type;
	uint32_t	vers;
	uint64_t	path;
} Qid;

typedef struct {
	uint16_t	type;
	uint32_t	dev;
	Qid		qid;
	uint32_t	mode;
	uint32_t	atime;
	uint32_t	mtime;
	uint64_t	length;
	char*		name;
	char*		uid;
	char*		gid;
	char*		muid;
} Dir;

/* from fcall(3) in plan9port */
typedef struct {
	uint8_t type;
	uint16_t tag;
	uint32_t fid;

	union {
		struct { /* Tversion, Rversion */
			uint32_t	msize;
			char		*version;
		};
		struct { /* Tflush */
			uint16_t	oldtag;
		};
		struct { /* Rerror */
			char		*ename;
		};
		struct { /* Ropen, Rcreate */
			Qid		qid;	/* +Rattach */
			uint32_t	iounit;
		};
		struct { /* Rauth */
			Qid		aqid;
		};
		struct { /* Tauth, Tattach */
			uint32_t	afid;
			char		*uname;
			char		*aname;
		};
		struct { /* Tcreate */
			uint32_t	perm;
			char		*name;
			uint8_t		mode;	/* +Topen */
		};
		struct { /* Rwalk */
			uint32_t	newfid;	/* +Twalk */
			union {
				uint16_t	nwname;
				uint16_t	nwqid;
			};
			union {
				char		**wname;
				Qid		*wqid;
			};
		};
		struct {
			uint64_t	offset;	/* Tread, Twrite */
			uint32_t	count;	/* Tread, Twrite, Rread */
		};
		struct { /* Rstat, Twstat */
			Dir		st;
		};
	};
} Fcall;

ssize_t  read9pmsg (int, uint8_t *, size_t);
ssize_t write9pmsg (int, uint8_t *);
ssize_t loadFcall (Fcall *, uint8_t *);
ssize_t storFcall (Fcall *, uint8_t *);
size_t loadDir (Dir *, uint8_t *);
size_t storDir (Dir *, uint8_t *);
size_t loadQid (Qid *, uint8_t *);
size_t storQid (Qid *, uint8_t *);
