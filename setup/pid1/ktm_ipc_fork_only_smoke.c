#include <unistd.h>
#include <sys/wait.h>

static void write_str(const char *s)
{
	const char *p = s;
	while (*p) p++;
	(void)write(1, s, (size_t)(p - s));
}

int main(void)
{
	pid_t pid = fork();
	if (pid == 0)
		_exit(0);
	if (pid > 0)
	{
		(void)waitpid(pid, NULL, 0);
		write_str("FASE48_FORK_OK\n");
	}
	for (;;)
		(void)pause();
	return 0;
}
