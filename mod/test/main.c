#include <kmod.h>
#include <std.h>

static int init(void)
{
	printf("init\n");
	return 0;
}

static void fini(void)
{
	printf("fini\n");
}

struct kmod_info kmod =
{
	.magic = KMOD_MAGIC,
	.version = 1,
	.name = "test",
	.init = init,
	.fini = fini,
};
