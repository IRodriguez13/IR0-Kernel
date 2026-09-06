/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * Verify AT_SYSINFO_EHDR + musl-style __vdsosym resolution for __vdso_clock_gettime.
 */
#include <elf.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>

extern char **environ;

static void emit(const char *s)
{
	size_t n = 0;

	while (s[n])
		n++;
	write(1, s, n);
}

static const Elf64_auxv_t *find_auxv(void)
{
	char **p = environ;

	while (*p)
		p++;
	return (const Elf64_auxv_t *)(p + 1);
}

static void *vdsosym(const Elf64_Ehdr *ehdr, const char *name)
{
	const Elf64_Phdr *ph;
	const Elf64_Dyn *dyn;
	const Elf64_Sym *sym;
	const char *str;
	Elf64_Addr dyn_addr = 0;
	Elf64_Addr symtab = 0;
	Elf64_Addr strtab = 0;
	uint64_t syment = sizeof(Elf64_Sym);
	uint64_t i;

	if (!ehdr || ehdr->e_ident[EI_MAG0] != ELFMAG0)
		return NULL;

	ph = (const Elf64_Phdr *)((const char *)ehdr + ehdr->e_phoff);
	for (i = 0; i < ehdr->e_phnum; i++)
	{
		if (ph[i].p_type == PT_DYNAMIC)
		{
			dyn_addr = ph[i].p_vaddr;
			break;
		}
	}
	if (!dyn_addr)
		return NULL;

	dyn = (const Elf64_Dyn *)(uintptr_t)dyn_addr;
	for (; dyn->d_tag != DT_NULL; dyn++)
	{
		if (dyn->d_tag == DT_SYMTAB)
			symtab = dyn->d_un.d_ptr;
		else if (dyn->d_tag == DT_STRTAB)
			strtab = dyn->d_un.d_ptr;
		else if (dyn->d_tag == DT_SYMENT)
			syment = dyn->d_un.d_val;
	}
	if (!symtab || !strtab)
		return NULL;

	sym = (const Elf64_Sym *)(uintptr_t)symtab;
	for (i = 1; ; i++)
	{
		const char *symstr;
		size_t n = 0;

		if (sym[i].st_name == 0 && sym[i].st_shndx == SHN_UNDEF &&
		    sym[i].st_value == 0)
			break;

		if (sym[i].st_shndx == SHN_UNDEF)
			continue;

		symstr = (const char *)(uintptr_t)(strtab + sym[i].st_name);
		while (name[n] && symstr[n] == name[n])
			n++;
		if (name[n] == symstr[n])
			return (void *)(uintptr_t)sym[i].st_value;
	}
	(void)syment;
	return NULL;
}

int main(void)
{
	const Elf64_auxv_t *aux;
	const Elf64_Ehdr *ehdr = NULL;
	int (*vdso_cgt)(int, void *);
	struct timespec ts;
	int ret;

	for (aux = find_auxv(); aux->a_type != AT_NULL; aux++)
	{
		if (aux->a_type == AT_SYSINFO_EHDR)
			ehdr = (const Elf64_Ehdr *)(uintptr_t)aux->a_un.a_val;
	}

	if (!ehdr)
	{
		emit("VDSO_AUXV_MISSING\n");
		return 1;
	}
	if (ehdr->e_ident[EI_MAG0] != ELFMAG0)
	{
		emit("VDSO_EHDR_BAD\n");
		return 1;
	}
	emit("VDSO_AUXV_OK\n");

	vdso_cgt = (int (*)(int, void *))vdsosym(ehdr, "__vdso_clock_gettime");
	if (!vdso_cgt)
	{
		emit("VDSO_SYM_FAIL\n");
		return 2;
	}
	emit("VDSO_SYM_OK\n");

	ret = vdso_cgt(1, &ts);
	if (ret != 0)
	{
		emit("VDSO_CALL_FAIL\n");
		return 3;
	}
	emit("VDSO_CALL_OK\n");
	return 0;
}
