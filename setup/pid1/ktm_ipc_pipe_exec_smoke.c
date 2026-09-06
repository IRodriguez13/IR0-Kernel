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
	int inpipe[2];
	int outpipe[2];
	char buf[8];
	int status = 0;
	pid_t pid;

	if (pipe2(inpipe, 0) < 0 || pipe2(outpipe, 0) < 0)
		return 1;

	pid = fork();
	if (pid == 0)
	{
		dup2(inpipe[0], 0);
		dup2(outpipe[1], 1);
		close(inpipe[0]);
		close(inpipe[1]);
		close(outpipe[0]);
		close(outpipe[1]);
		execl("/bin/cat", "cat", (char *)NULL);
		_exit(127);
	}

	close(inpipe[0]);
	close(outpipe[1]);
	(void)write(inpipe[1], "hi\n", 3);
	close(inpipe[1]);
	if (waitpid(pid, &status, 0) < 0)
		return 1;
	if (read(outpipe[0], buf, sizeof(buf) - 1) < 2)
	{
		write_str("FASE48_PIPE_EXEC_READ_FAIL\n");
		return 1;
	}
	close(outpipe[0]);
	write_str("FASE48_PIPE_EXEC_OK\n");
	for (;;)
		(void)pause();
	return 0;
}
