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
	int pc[2];
	char c;
	pid_t pid;

	pipe2(pc, 0);
	pid = fork();
	if (pid == 0)
	{
		close(pc[1]);
		if (read(pc[0], &c, 1) != 1)
			_exit(1);
		close(pc[0]);
		_exit(0);
	}
	close(pc[0]);
	(void)write(pc[1], "X", 1);
	close(pc[1]);
	(void)waitpid(pid, NULL, 0);
	write_str("FASE48_PIPE_READ_OK\n");
	for (;;)
		(void)pause();
	return 0;
}
