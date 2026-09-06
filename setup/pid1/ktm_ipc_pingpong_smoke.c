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
	char c;
	pid_t pid;

	pipe2(pc, 0);
	pipe2(cp, 0);
	pid = fork();
	if (pid == 0)
	{
		close(pc[1]);
		close(cp[0]);
		if (read(pc[0], &c, 1) != 1)
			_exit(1);
		(void)write(cp[1], "C", 1);
		close(pc[0]);
		close(cp[1]);
		_exit(0);
	}
	close(pc[0]);
	(void)write(pc[1], "P", 1);
	close(pc[1]);
	(void)waitpid(pid, NULL, 0);
	if (read(cp[0], &c, 1) != 1 || c != 'C')
	{
		write_str("FAIL\n");
		return 1;
	}
	close(cp[0]);
	close(cp[1]);
	write_str("FASE48_PINGPONG_OK\n");
	for (;;)
		(void)pause();
	return 0;
}
