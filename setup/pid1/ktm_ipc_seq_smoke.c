#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>

static void write_str(const char *s)
{
	const char *p = s;
	while (*p) p++;
	(void)write(1, s, (size_t)(p - s));
}

static int pingpong(void)
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
		return -1;
	close(cp[0]);
	close(cp[1]);
	return 0;
}

static int pipe_exec(void)
{
	int inpipe[2], outpipe[2];
	char buf[8];
	pid_t pid;

	pipe2(inpipe, 0);
	pipe2(outpipe, 0);
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
	(void)waitpid(pid, NULL, 0);
	if (read(outpipe[0], buf, sizeof(buf) - 1) < 2)
		return -1;
	close(outpipe[0]);
	return 0;
}

int main(void)
{
	if (pingpong() != 0)
		write_str("PING_FAIL\n");
	else
		write_str("PING_OK\n");
	if (pipe_exec() != 0)
		write_str("EXEC_FAIL\n");
	else
		write_str("EXEC_OK\n");
	for (;;)
		(void)pause();
	return 0;
}
