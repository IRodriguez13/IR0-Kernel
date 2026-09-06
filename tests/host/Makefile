# IR0 Kernel - Test suite Makefile
# Solo se activa con make tests desde la raíz (o make -C test).
# Los tests se compilan con libc del host; no se usan includes del kernel
# para evitar conflictos (stdint, stdarg, etc.). Para tests que enlacen
# código del kernel haría falta un árbol de stubs.

# Repository root (this Makefile lives in tests/host/).
KERNEL_ROOT ?= ../..
CC         = gcc
BASE_CFLAGS = -Wall -Wextra -g -I. -DTEST_HOST
# idirafter: resolve <ir0/...> after system headers so host <string.h> wins over includes/string.h
HOST_KERNEL_CPPFLAGS = -I$(KERNEL_ROOT) -I$(KERNEL_ROOT)/ktm/include \
	-I$(KERNEL_ROOT)/includes/uapi \
	-idirafter $(KERNEL_ROOT)/includes \
	-I$(KERNEL_ROOT)/tests/mocks
KERNEL_CFLAGS = $(BASE_CFLAGS) $(HOST_KERNEL_CPPFLAGS)
LDFLAGS    =

TEST_SRCS  = test_runner.c test_example.c \
	test_pseudo_fs_registry.c test_pseudo_fs_proc_registry.c \
	test_pseudo_fs_children.c \
	test_usb_pci_scan.c test_bt_scan_sync.c \
	test_vfs_backend_contract.c 	test_stat_user_abi.c \
	test_named_fifo_supervise.c 	test_path_resolve_at.c \
	test_path_chroot.c \
	test_ktm_sched_contract.c test_class_b_ctx_invariant.c test_musl_cred_abi.c test_musl_mmap_contract.c \
	test_signal_rt_sigaction_abi.c test_sigreturn_sleep_eintr_abi.c test_mmap_null_placement.c \
	test_elf_initial_brk_abi.c test_blockdev_facade.c \
	test_arch_irq_facade.c test_arch_mm_pte_facade.c \
	test_ps2_set1_modifiers.c \
	test_ps2_mouse_pkt.c \
	test_pipe_close_refcount.c \
	test_rtc_calendar.c \
	test_arch_task_contract.c \
	test_pseudo_fs_contract.c \
	test_mm_mirror_contract.c \
	test_netdev_contract.c \
	test_sched_backend_contract.c \
	test_block_backend_contract.c \
	test_usercopy_contract.c \
	test_matrix_capture.c
KERNEL_HOST_OBJS = host_pseudo_fs_registry.o host_usb_host_disabled.o \
	host_rtc_calendar.o \
	host_kmem_stub.o host_serial_stub.o host_klog_stub.o host_fase48_stub.o \
	host_sysfs_net_stub.o \
	host_named_fifo.o host_path.o host_pipe.o host_blockdev.o \
	host_arch_irq_stub.o host_arch_mm_stub.o \
	host_ps2_set1.o host_ps2_mouse_pkt.o \
	host_matrix_capture.o
TEST_OBJS  = $(TEST_SRCS:.c=.o) $(KERNEL_HOST_OBJS)
TEST_BIN   = ir0_test_suite

IR0_USERSPACE_ROOT ?= $(abspath $(KERNEL_ROOT)/../IR0-userspace)
MATRIX_CAPTURE_CFLAGS = $(BASE_CFLAGS) -I$(IR0_USERSPACE_ROOT)/smoke

.PHONY: all clean memsafe run

all: $(TEST_BIN)

host_pseudo_fs_registry.o: $(KERNEL_ROOT)/fs/pseudo_fs_registry.c
	$(CC) $(KERNEL_CFLAGS) -c -o $@ $<

host_usb_host_disabled.o: $(KERNEL_ROOT)/drivers/usb/usb_host.c
	$(CC) $(KERNEL_CFLAGS) -DCONFIG_ENABLE_USB_HOST=0 -c -o $@ $<

host_kmem_stub.o: host_kmem_stub.c
	$(CC) $(BASE_CFLAGS) -c -o $@ $<

host_serial_stub.o: host_serial_stub.c
	$(CC) $(KERNEL_CFLAGS) -c -o $@ $<

host_klog_stub.o: host_klog_stub.c
	$(CC) $(KERNEL_CFLAGS) -c -o $@ $<

host_fase48_stub.o: host_fase48_stub.c
	$(CC) $(BASE_CFLAGS) -c -o $@ $<

host_sysfs_net_stub.o: host_sysfs_net_stub.c
	$(CC) $(KERNEL_CFLAGS) -c -o $@ $<

host_named_fifo.o: $(KERNEL_ROOT)/kernel/lib/named_fifo.c
	$(CC) $(KERNEL_CFLAGS) -DTEST_HOST -DCONFIG_DEBUG_FASE50=0 -c -o $@ $<

host_path.o: $(KERNEL_ROOT)/fs/path.c
	$(CC) $(KERNEL_CFLAGS) -c -o $@ $<

host_pipe.o: $(KERNEL_ROOT)/kernel/lib/pipe.c
	$(CC) $(KERNEL_CFLAGS) -DCONFIG_DEBUG_FASE50=0 -c -o $@ $<

host_blockdev.o: $(KERNEL_ROOT)/kernel/lib/blockdev.c
	$(CC) $(KERNEL_CFLAGS) -DTEST_HOST -c -o $@ $<

host_arch_irq_stub.o: host_arch_irq_stub.c
	$(CC) $(BASE_CFLAGS) -c -o $@ $<

host_arch_mm_stub.o: host_arch_mm_stub.c
	$(CC) $(BASE_CFLAGS) -c -o $@ $<

host_ps2_set1.o: $(KERNEL_ROOT)/kernel/lib/ps2_set1.c
	$(CC) $(KERNEL_CFLAGS) -c -o $@ $<

host_ps2_mouse_pkt.o: $(KERNEL_ROOT)/kernel/lib/ps2_mouse_pkt.c
	$(CC) $(KERNEL_CFLAGS) -c -o $@ $<

host_rtc_calendar.o: $(KERNEL_ROOT)/kernel/lib/rtc_calendar.c
	$(CC) $(KERNEL_CFLAGS) -c -o $@ $<

host_matrix_capture.o: $(IR0_USERSPACE_ROOT)/smoke/matrix_capture.c
	$(CC) $(MATRIX_CAPTURE_CFLAGS) -c -o $@ $<

test_matrix_capture.o: test_matrix_capture.c
	$(CC) $(MATRIX_CAPTURE_CFLAGS) -c -o $@ $<

test_rtc_calendar.o: test_rtc_calendar.c
	$(CC) $(KERNEL_CFLAGS) -c -o $@ $<

test_ps2_set1_modifiers.o: test_ps2_set1_modifiers.c
	$(CC) $(KERNEL_CFLAGS) -c -o $@ $<

test_ps2_mouse_pkt.o: test_ps2_mouse_pkt.c
	$(CC) $(KERNEL_CFLAGS) -c -o $@ $<

test_pipe_close_refcount.o: test_pipe_close_refcount.c
	$(CC) $(KERNEL_CFLAGS) -c -o $@ $<

test_runner.o: test_runner.c
	$(CC) $(BASE_CFLAGS) -c -o $@ $<

test_blockdev_facade.o: test_blockdev_facade.c
	$(CC) $(KERNEL_CFLAGS) -DTEST_HOST -c -o $@ $<

test_arch_irq_facade.o: test_arch_irq_facade.c
	$(CC) $(BASE_CFLAGS) -c -o $@ $<

test_arch_mm_pte_facade.o: test_arch_mm_pte_facade.c
	$(CC) $(BASE_CFLAGS) -c -o $@ $<

test_example.o: test_example.c
	$(CC) $(BASE_CFLAGS) -c -o $@ $<

test_pseudo_fs_proc_registry.o: test_pseudo_fs_proc_registry.c
	$(CC) $(KERNEL_CFLAGS) -c -o $@ $<

test_pseudo_fs_registry.o: test_pseudo_fs_registry.c
	$(CC) $(KERNEL_CFLAGS) -c -o $@ $<

test_pseudo_fs_children.o: test_pseudo_fs_children.c
	$(CC) $(KERNEL_CFLAGS) -c -o $@ $<

test_usb_pci_scan.o: test_usb_pci_scan.c
	$(CC) $(KERNEL_CFLAGS) -c -o $@ $<

test_bt_scan_sync.o: test_bt_scan_sync.c
	$(CC) $(BASE_CFLAGS) -c -o $@ $<

test_vfs_backend_contract.o: test_vfs_backend_contract.c
	$(CC) $(KERNEL_CFLAGS) -c -o $@ $<

test_stat_user_abi.o: test_stat_user_abi.c
	$(CC) $(BASE_CFLAGS) -c -o $@ $<

test_named_fifo_supervise.o: test_named_fifo_supervise.c
	$(CC) $(KERNEL_CFLAGS) -c -o $@ $<

test_path_resolve_at.o: test_path_resolve_at.c
	$(CC) $(KERNEL_CFLAGS) -c -o $@ $<

test_path_chroot.o: test_path_chroot.c
	$(CC) $(KERNEL_CFLAGS) -c -o $@ $<

test_ktm_sched_contract.o: test_ktm_sched_contract.c
	$(CC) $(BASE_CFLAGS) -c -o $@ $<

test_class_b_ctx_invariant.o: test_class_b_ctx_invariant.c
	$(CC) $(KERNEL_CFLAGS) -c -o $@ $<

test_musl_mmap_contract.o: test_musl_mmap_contract.c
	$(CC) $(KERNEL_CFLAGS) -c -o $@ $<

test_musl_cred_abi.o: test_musl_cred_abi.c
	$(CC) $(KERNEL_CFLAGS) -c -o $@ $<

test_signal_rt_sigaction_abi.o: test_signal_rt_sigaction_abi.c
	$(CC) $(KERNEL_CFLAGS) -c -o $@ $<

test_sigreturn_sleep_eintr_abi.o: test_sigreturn_sleep_eintr_abi.c
	$(CC) $(KERNEL_CFLAGS) -c -o $@ $<

test_mmap_null_placement.o: test_mmap_null_placement.c
	$(CC) $(KERNEL_CFLAGS) -c -o $@ $<

test_elf_initial_brk_abi.o: test_elf_initial_brk_abi.c
	$(CC) $(KERNEL_CFLAGS) -c -o $@ $<

test_arch_task_contract.o: test_arch_task_contract.c
	$(CC) $(KERNEL_CFLAGS) -c -o $@ $<

test_pseudo_fs_contract.o: test_pseudo_fs_contract.c
	$(CC) $(KERNEL_CFLAGS) -c -o $@ $<

test_mm_mirror_contract.o: test_mm_mirror_contract.c
	$(CC) $(KERNEL_CFLAGS) -c -o $@ $<

test_netdev_contract.o: test_netdev_contract.c
	$(CC) $(KERNEL_CFLAGS) -c -o $@ $<

test_sched_backend_contract.o: test_sched_backend_contract.c
	$(CC) $(KERNEL_CFLAGS) -c -o $@ $<

test_block_backend_contract.o: test_block_backend_contract.c
	$(CC) $(KERNEL_CFLAGS) -c -o $@ $<

$(TEST_BIN): $(TEST_OBJS)
	$(CC) -o $@ $(TEST_OBJS) $(LDFLAGS)

run: $(TEST_BIN)
	./$(TEST_BIN)

memsafe: $(TEST_BIN)
	valgrind --leak-check=full --error-exitcode=1 ./$(TEST_BIN)

clean:
	rm -f $(TEST_OBJS) $(TEST_BIN)
