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
	int p1[2], p2[2];
	pid_t pid;

	pipe2(p1, 0);
	pipe2(p2, 0);
	pid = fork();
	if (pid == 0)
		_exit(0);
	(void)waitpid(pid, NULL, 0);
	close(p1[0]); close(p1[1]); close(p2[0]); close(p2[1]);
	write_str("FASE48_FORK_PIPES_OPEN_OK\n");
	for (;;)
		(void)pause();
	return 0;
}
