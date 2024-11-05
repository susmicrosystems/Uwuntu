#ifndef SYS_UTSNAME_H
#define SYS_UTSNAME_H

#ifdef __cplusplus
extern "C" {
#endif

struct utsname
{
	char sysname[256];
	char nodename[256];
	char release[256];
	char version[256];
	char machine[256];
};

int uname(struct utsname *buf);

#ifdef __cplusplus
}
#endif

#endif
