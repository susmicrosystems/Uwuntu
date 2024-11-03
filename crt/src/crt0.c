#include <stddef.h>

int main(int argc, char **argv, char **envp);
int __libc_start_main(int (*main)(int, char**, char**), int argc, char **argv, char **envp, size_t *auxv);

int _start(int argc, char **argv, char **envp, size_t *auxv)
{
	return __libc_start_main(main, argc, argv, envp, auxv);
}
