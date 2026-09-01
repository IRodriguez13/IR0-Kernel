// SPDX-License-Identifier: GPL-3.0-only
/**
 * IR0 Kernel — Linux open(2) ABI → IR0 internal open flags translation.
 */

#include <ir0/open_flags.h>
#include <ir0/ktm/klog.h>
#include <string.h>

int linux_open_flags_to_ir0(int linux_flags)
{
    int ir0 = linux_flags & IR0_O_ACCMODE;

    if (linux_flags & (int)LINUX_O_CREAT)
        ir0 |= IR0_O_CREAT;
    if (linux_flags & (int)LINUX_O_EXCL)
        ir0 |= IR0_O_EXCL;
    if (linux_flags & (int)LINUX_O_TRUNC)
        ir0 |= IR0_O_TRUNC;
    if (linux_flags & (int)LINUX_O_APPEND)
        ir0 |= IR0_O_APPEND;
    if (linux_flags & (int)LINUX_O_NONBLOCK)
        ir0 |= IR0_O_NONBLOCK;
    if (linux_flags & (int)LINUX_O_DIRECTORY)
        ir0 |= IR0_O_DIRECTORY;
    if (linux_flags & (int)LINUX_O_CLOEXEC)
        ir0 |= IR0_O_CLOEXEC;

    return ir0;
}

int ir0_open_flags_ok_for_vfs(int flags)
{
    uint32_t f = (uint32_t)flags;

    if (f & LINUX_O_CREAT)
        return 0;
    if (f & LINUX_O_EXCL)
        return 0;

    if ((f & LINUX_O_DIRECTORY) && !(f & IR0_O_DIRECTORY))
        return 0;
    if (f & LINUX_O_NOFOLLOW)
        return 0;

    if ((f & LINUX_O_TRUNC) && !(f & IR0_O_EXCL) && !(f & IR0_O_TRUNC))
        return 0;

    return 1;
}

void ir0_open_flags_log_translation(int linux_raw, int ir0_flags)
{
    char flags[128];
    size_t off = 0;

    if (!klog_trace_enabled(KLOG_TRACE_OPEN_ABI))
        return;

    flags[0] = '\0';
#define APPEND_FLAG(bit, name) do { \
	if (ir0_flags & (bit)) { \
		const char *s = (off == 0) ? (name) : "|" name; \
		size_t sl = strlen(s); \
		if (off + sl < sizeof(flags)) { \
			memcpy(flags + off, s, sl + 1); \
			off += sl; \
		} \
	} \
} while (0)

    APPEND_FLAG(IR0_O_CREAT, "CREAT");
    APPEND_FLAG(IR0_O_TRUNC, "TRUNC");
    APPEND_FLAG(IR0_O_EXCL, "EXCL");
    APPEND_FLAG(IR0_O_APPEND, "APPEND");
    APPEND_FLAG(IR0_O_NONBLOCK, "NONBLOCK");
    APPEND_FLAG(IR0_O_DIRECTORY, "DIRECTORY");
    APPEND_FLAG(IR0_O_CLOEXEC, "CLOEXEC");
    if ((ir0_flags & IR0_O_ACCMODE) == IR0_O_WRONLY)
        APPEND_FLAG(IR0_O_WRONLY, "WRONLY");
    else if ((ir0_flags & IR0_O_ACCMODE) == IR0_O_RDWR)
        APPEND_FLAG(IR0_O_RDWR, "RDWR");
    else if ((ir0_flags & IR0_O_ACCMODE) == IR0_O_RDONLY)
    {
        const char *s = (off == 0) ? "RDONLY" : "|RDONLY";
        size_t sl = strlen(s);
        if (off + sl < sizeof(flags))
        {
            memcpy(flags + off, s, sl + 1);
            off += sl;
        }
    }
#undef APPEND_FLAG

    if (off == 0)
        klog_trace_fmt("OPEN_ABI", "raw=0x%x ir0=0x%x",
                       (unsigned)(uint32_t)linux_raw,
                       (unsigned)(uint32_t)ir0_flags);
    else
        klog_trace_fmt("OPEN_ABI", "raw=0x%x ir0=0x%x flags=%s",
                       (unsigned)(uint32_t)linux_raw,
                       (unsigned)(uint32_t)ir0_flags, flags);

    if (ir0_open_flags_ok_for_vfs(ir0_flags))
        klog_trace("OPEN_ABI", "CLASSIFY OPEN_ABI_TRANSLATION_LAYER_OK");
}
