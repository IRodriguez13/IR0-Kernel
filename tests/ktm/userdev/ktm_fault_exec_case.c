/**
 * IR0 userspace — KTM fault injection
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: ktm_fault_exec_case.c
 * Description: Arm exec.read_file / exec.setup_stack; assert no leaks.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

#include "libktm_user.h"

/*
 * Both hooks sit on either side of the point of no return in
 * exec_replace_current:
 *
 *   exec.read_file   — before the commit; the caller must get an error back
 *                      and keep running its old image.
 *   exec.setup_stack — after it; the old image is gone, so the process must
 *                      die and leave nothing behind.
 *
 * Neither branch runs under normal load, which is how a leak of the argv/envp
 * copies survived in the success path long enough to fragment the kernel heap
 * and make exec itself start failing after a few hundred spawns.
 */

static void say(const char *s)
{
	(void)write(1, s, strlen(s));
}

static int spawn_and_wait(void)
{
	pid_t pid = fork();
	int status = 0;

	if (pid < 0)
		return -1;

	if (pid == 0)
	{
		char *const av[] = { "true", NULL };

		execve("/bin/f41true", av, NULL);
		/* Pre-commit failure: exec returned, the old image still runs. */
		_exit(42);
	}

	if (waitpid(pid, &status, 0) != pid)
		return -1;

	return status;
}

int main(void)
{
	int kfd;
	int fails = 0;
	ktm_user_caps_t caps;
	ktm_ioc_snapshot_t before;
	int status;

	kfd = ktm_open();
	if (kfd < 0)
	{
		say("KTM_FAULT_EXEC_FAIL open\n");
		return 1;
	}
	if (ktm_get_caps(kfd, &caps) != 0 || !(caps.caps & KTM_CAP_USERDEV))
	{
		say("KTM_FAULT_EXEC_FAIL caps\n");
		ktm_close(kfd);
		return 1;
	}
	if (!(caps.caps & KTM_CAP_FAULT))
	{
		say("KTM_FAULT_EXEC_SKIP no_cap_fault\n");
		ktm_close(kfd);
		return 0;
	}

	(void)ktm_reset(kfd);
	if (ktm_case_begin(kfd, "fault_exec") != 0)
	{
		say("KTM_FAULT_EXEC_FAIL case_begin\n");
		ktm_close(kfd);
		return 1;
	}

	if (ktm_snapshot_request(kfd, &before) != 0)
		fails++;

	/*
	 * Baseline first. Exit 42 only means "execve returned", which is also
	 * what a missing /bin/f41true produces — without this the fault
	 * assertions below pass on an image that never had the binary.
	 */
	(void)ktm_checkpoint(kfd, "exec_baseline");
	status = spawn_and_wait();
	{
		int ok = status >= 0 && WIFEXITED(status) &&
			 WEXITSTATUS(status) != 42;

		(void)ktm_assert_true(kfd, "exec_baseline", ok);
		if (!ok)
		{
			fails++;
			goto done;
		}
	}

	/* Pre-commit: exec must fail and the child must reach its own _exit. */
	(void)ktm_checkpoint(kfd, "arm_exec_read_file");
	if (ktm_config_fault(kfd, "exec.read_file", KTM_FAULT_MODE_ONCE, 0, 0) != 0)
	{
		(void)ktm_assert_true(kfd, "config_fault_read", 0);
		fails++;
		goto done;
	}
	(void)ktm_assert_true(kfd, "config_fault_read", 1);

	status = spawn_and_wait();
	if (status < 0)
	{
		(void)ktm_assert_true(kfd, "read_file_spawn", 0);
		fails++;
	}
	else
	{
		int hit = WIFEXITED(status) && WEXITSTATUS(status) == 42;

		(void)ktm_assert_true(kfd, "read_file_fault_hit", hit);
		if (!hit)
			fails++;
		else
			say("KTM_FAULT_EXEC_PRECOMMIT_OK\n");
	}

	/* Post-commit: the image is gone, so the child must not survive. */
	(void)ktm_checkpoint(kfd, "arm_exec_setup_stack");
	if (ktm_config_fault(kfd, "exec.setup_stack", KTM_FAULT_MODE_ONCE, 0, 0) != 0)
	{
		(void)ktm_assert_true(kfd, "config_fault_stack", 0);
		fails++;
		goto done;
	}
	(void)ktm_assert_true(kfd, "config_fault_stack", 1);

	status = spawn_and_wait();
	if (status < 0)
	{
		(void)ktm_assert_true(kfd, "setup_stack_spawn", 0);
		fails++;
	}
	else
	{
		/* Killed by exec_fail_kill(127), never the child's own _exit(42). */
		int died = !(WIFEXITED(status) && WEXITSTATUS(status) == 42);

		(void)ktm_assert_true(kfd, "setup_stack_fault_hit", died);
		if (!died)
			fails++;
		else
			say("KTM_FAULT_EXEC_POSTCOMMIT_OK\n");
	}

	/* Recovery: with no fault armed, exec works again. */
	(void)ktm_checkpoint(kfd, "exec_recover");
	status = spawn_and_wait();
	{
		int ok = status >= 0 && WIFEXITED(status) &&
			 WEXITSTATUS(status) != 42;

		(void)ktm_assert_true(kfd, "exec_recover", ok);
		if (!ok)
			fails++;
	}

	fails += ktm_assert_no_leaks(kfd, &before);

done:
	(void)ktm_case_end(kfd, "fault_exec", fails == 0 ? 0 : 1);
	ktm_close(kfd);

	if (fails == 0)
	{
		say("KTM_FAULT_EXEC_OK\n");
		say("KTM_USERDEV_OK\n");
		_exit(0);
	}
	say("KTM_FAULT_EXEC_FAIL\n");
	_exit(1);
}
