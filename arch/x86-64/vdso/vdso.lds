/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * Fixed vDSO load address — must match VDSO_USER_BASE in includes/ir0/abi/vdso_contract.h.
 * ELF ehdr must live at VDSO_BASE (AT_SYSINFO_EHDR); musl reads e_phoff from there.
 */
OUTPUT_FORMAT("elf64-x86-64", "elf64-x86-64", "elf64-x86-64")
OUTPUT_ARCH(i386:x86-64)

VDSO_BASE = 0x7FD00000;

PHDRS
{
	text PT_LOAD FLAGS(5); /* PF_R | PF_X */
	dynamic PT_DYNAMIC FLAGS(4); /* PF_R */
}

SECTIONS
{
	. = VDSO_BASE + SIZEOF_HEADERS;

	.text : {
		*(.text*)
	} :text

	. = ALIGN(8);
	.dynamic : {
		*(.dynamic)
	} :text :dynamic

	.dynsym : {
		*(.dynsym)
	} :text :dynamic

	.dynstr : {
		*(.dynstr)
	} :text :dynamic

	.hash : {
		*(.hash)
	} :text :dynamic

	.gnu.hash : {
		*(.gnu.hash)
	} :text :dynamic

	.gnu.version : {
		*(.gnu.version)
	} :text :dynamic

	.gnu.version_d : {
		*(.gnu.version_d)
	} :text :dynamic

	.gnu.version_r : {
		*(.gnu.version_r)
	} :text :dynamic

	/DISCARD/ : {
		*(.comment)
		*(.note.GNU-stack)
	}
}
