#include <readline/readline.h>
#include <readline/history.h>

#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/user.h>

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <signal.h>
#include <libdbg.h>
#include <stdio.h>
#include <errno.h>

struct env
{
	const char *progname;
	int argc;
	char **argv;
	pid_t child;
	int child_signum;
};

static int launch_child(struct env *env)
{
	env->child = fork();
	if (env->child == -1)
	{
		fprintf(stderr, "%s: fork: %s\n", env->progname,
		        strerror(errno));
		return 1;
	}
	if (!env->child)
	{
		if (ptrace(PTRACE_TRACEME, 0, 0, 0) == -1)
		{
			fprintf(stderr, "%s: ptrace(PTRACE_TRACEME): %s\n",
			        env->progname, strerror(errno));
			exit(EXIT_FAILURE);
		}
		execvp(env->argv[0], env->argv);
		fprintf(stderr, "%s: execve: %s\n", env->progname,
		        strerror(errno));
		exit(EXIT_FAILURE);
	}
	while (1)
	{
		int wstatus;
		if (waitpid(env->child, &wstatus, 0) == -1)
		{
			if (errno == EINTR)
				continue;
			fprintf(stderr, "%s: waitpid: %s\n", env->progname,
			        strerror(errno));
			return 1;
		}
		if (ptrace(PTRACE_CONT, env->child, 0, 0))
		{
			fprintf(stderr, "%s: ptrace(PTRACE_CONT): %s\n",
			        env->progname, strerror(errno));
			return 1;
		}
		return 0;
	}
}

static int child_wait(struct env *env)
{
	while (1)
	{
		int wstatus;
		pid_t pid = waitpid(env->child, &wstatus, 0);
		if (pid == -1)
		{
			if (errno == EINTR)
				continue;
		}
		if (WIFEXITED(wstatus))
		{
			printf("[process %" PRId32 " terminated with code %d]\n",
			       pid, WEXITSTATUS(wstatus));
			env->child = -1;
			return 0;
		}
		if (WIFSIGNALED(wstatus))
		{
			const struct dbg_signal *sig = dbg_signal_get(WTERMSIG(wstatus));
			if (sig)
				printf("[process %" PRId32 " killed by signal %s]\n",
				       pid, sig->name);
			else
				printf("[process %" PRId32 " killed by signal %d]\n",
				       pid, WTERMSIG(wstatus));
			env->child = -1;
			return 0;
		}
		if (!WIFSTOPPED(wstatus))
		{
			fprintf(stderr, "unhandled wstatus %x\n", wstatus);
			return 1;
		}
		env->child_signum = WSTOPSIG(wstatus);
		if (env->child_signum == SIGTRAP)
		{
			/* XXX take action on caller (e.g: "breakpoint reach") */
			env->child_signum = 0;
			return 0;
		}
		const struct dbg_signal *sig = dbg_signal_get(env->child_signum);
		if (sig)
			printf("[process %" PRId32 " received signal %s]\n",
			       pid, sig->name);
		else
			printf("[process %" PRId32 " received signal %d]\n",
			       pid, env->child_signum);
		return 0;
	}
}

static int cmd_run(struct env *env)
{
	if (env->child != -1)
	{
		printf("child is already running\n");
		return 0;
	}
	if (launch_child(env))
		return 1;
	return child_wait(env);
}

static int cmd_info_reg(struct env *env)
{
	struct user_regs_struct regs;
	if (ptrace(PTRACE_GETREGS, env->child, 0, &regs))
	{
		fprintf(stderr, "%s: ptrace(PTRACE_GETREGS): %s\n",
		       env->progname, strerror(errno));
		return 1;
	}
#if defined(__i386__)

#define PRINT_REG(r) printf("%-5s 0x%08" PRIx32 "   %" PRId32 "\n", \
                            #r, regs.r, (int32_t)regs.r);
	PRINT_REG(eax);
	PRINT_REG(ebx);
	PRINT_REG(ecx);
	PRINT_REG(edx);
	PRINT_REG(esi);
	PRINT_REG(edi);
	PRINT_REG(ebp);
	PRINT_REG(esp);
	PRINT_REG(eip);
	PRINT_REG(ef);
	PRINT_REG(cs);
	PRINT_REG(ds);
	PRINT_REG(es);
	PRINT_REG(fs);
	PRINT_REG(gs);
#undef PRINT_REG

#elif defined(__x86_64__)

#define PRINT_REG(r) printf("%-5s 0x%016" PRIx64 "   %" PRId64 "\n", \
                            #r, regs.r, (int64_t)regs.r);
	PRINT_REG(rax);
	PRINT_REG(rbx);
	PRINT_REG(rcx);
	PRINT_REG(rdx);
	PRINT_REG(rdi);
	PRINT_REG(rsi);
	PRINT_REG(rbp);
	PRINT_REG(rsp);
	PRINT_REG(r8);
	PRINT_REG(r9);
	PRINT_REG(r10);
	PRINT_REG(r11);
	PRINT_REG(r12);
	PRINT_REG(r13);
	PRINT_REG(r14);
	PRINT_REG(r15);
	PRINT_REG(rip);
	PRINT_REG(rf);
	PRINT_REG(cs);
	PRINT_REG(ds);
	PRINT_REG(es);
	PRINT_REG(fs);
	PRINT_REG(gs);
#undef PRINT_REG

#elif defined(__arm__)

	for (int i = 0; i < 32; ++i)
		printf("r%-4d 0x%08" PRIx32 "   %" PRId32 "\n",
		      i, regs.r[i], (int32_t)regs.r[i]);

#elif defined(__aarch64__)

	for (int i = 0; i < 32; ++i)
		printf("r%-4d 0x%016" PRIx64 "   %" PRId64 "\n",
		      i, regs.x[i], (int64_t)regs.x[i]);

#elif defined(__riscv_xlen) && __riscv_xlen == 32

	for (int i = 0; i < 32; ++i)
		printf("r%-4d 0x%08" PRIx32 "   %" PRId32 "\n",
		      i, regs.r[i], (int32_t)regs.r[i]);

#elif defined(__riscv_xlen) && __riscv_xlen == 64

	for (int i = 0; i < 32; ++i)
		printf("r%-4d 0x%016" PRIx64 "   %" PRId64 "\n",
		      i, regs.r[i], (int64_t)regs.r[i]);

#else
# error "unknown arch"
#endif
	return 0;
}

static int cmd_continue(struct env *env)
{
	if (env->child == -1)
	{
		printf("no child to continue\n");
		return 0;
	}
	if (ptrace(PTRACE_CONT, env->child, 0, env->child_signum))
	{
		fprintf(stderr, "%s: ptrace(PTRACE_CONT): %s\n",
		        env->progname, strerror(errno));
		return 1;
	}
	env->child_signum = 0;
	return child_wait(env);
}

static int cmd_stepi(struct env *env)
{
	if (env->child == -1)
	{
		printf("no child to continue\n");
		return 0;
	}
	if (ptrace(PTRACE_SINGLESTEP, env->child, 0, env->child_signum))
	{
		fprintf(stderr, "%s: ptrace(PTRACE_SINGLESTEP): %s\n",
		        env->progname, strerror(errno));
		return 1;
	}
	env->child_signum = 0;
	return child_wait(env);
}

static int exec_line(struct env *env, const char *line)
{
	if (!strcmp(line, "run"))
		return cmd_run(env);
	if (!strcmp(line, "info reg"))
		return cmd_info_reg(env);
	if (!strcmp(line, "continue"))
		return cmd_continue(env);
	if (!strcmp(line, "stepi"))
		return cmd_stepi(env);
	if (!strcmp(line, "help"))
	{
		printf("help: show this help\n");
		printf("run: run the program\n");
	}
	printf("unknown command: \"%s\"\n", line);
	return 0;
}

static int hist_up(void)
{
	HIST_ENTRY *entry = previous_history();
	if (!entry)
		return 0;
	rl_delete_text(0, rl_end);
	rl_insert_text(entry->line);
	return 0;
}

static int hist_down(void)
{
	HIST_ENTRY *entry = next_history();
	if (!entry)
	{
		rl_delete_text(0, rl_end);
		return 0;
	}
	rl_delete_text(0, rl_end);
	rl_insert_text(entry->line);
	return 0;
}

static int run_interactive(struct env *env)
{
	using_history();
	stifle_history(100);
	rl_generic_bind(ISFUNC, "\033[A", hist_up, rl_get_keymap());
	rl_generic_bind(ISFUNC, "\033[B", hist_down, rl_get_keymap());
	while (1)
	{
		char *line = readline("jdb) ");
		if (!line)
		{
			fprintf(stderr, "%s: readline: %s\n", env->progname,
			        strerror(errno));
			return 1;
		}
		if (line && *line && *line != ' ')
			add_history(line);
		while (next_history())
			;
		if (exec_line(env, line))
		{
			free(line);
			return 1;
		}
		free(line);
	}
	return 0;
}

static void usage(const char *progname)
{
	printf("%s [-h] PROGRAM ARGS\n", progname);
	printf("-h: show this help\n");
}

int main(int argc, char **argv)
{
	struct env env;
	int c;

	if (!isatty(0))
	{
		fprintf(stderr, "%s must be run in a terminal\n", argv[0]);
		return EXIT_FAILURE;
	}
	memset(&env, 0, sizeof(env));
	env.progname = argv[0];
	env.child = -1;
	while ((c = getopt(argc, argv, "h")) != -1)
	{
		switch (c)
		{
			case 'h':
				usage(argv[0]);
				return EXIT_SUCCESS;
			default:
				usage(argv[0]);
				return EXIT_FAILURE;
		}
	}
	if (argc - optind < 1)
	{
		fprintf(stderr, "%s: missing operand\n", argv[0]);
		return EXIT_FAILURE;
	}
	env.argc = argc - optind;
	env.argv = &argv[optind];
	if (run_interactive(&env))
		return EXIT_FAILURE;
	return EXIT_SUCCESS;
}
