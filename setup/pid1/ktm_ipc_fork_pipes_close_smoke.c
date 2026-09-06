#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>

static void write_str(const char *s)
{
	const char *p = s;
	while (*p) p++;
	(void)write(1, s, (size_t)(p - s));
}

int main(void)
{
	int pc[2], cp[2];
	pid_t pid;

	pipe2(pc, 0);
	pipe2(cp, 0);
	pid = fork();
	if (pid == 0)
	{
		close(pc[1]);
		close(cp[0]);
		close(pc[0]);
		close(cp[1]);
		_exit(0);
	}
	
	close(pc[0]);
	close(cp[1]);
	close(pc[1]);
	close(cp[0]);
	(void)waitpid(pid, NULL, 0);
	write_str("FASE48_FORK_PIPES_CLOSE_OK\n");
	for (;;)
		(void)pause();
	return 0;
}
