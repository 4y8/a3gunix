#include "../libc/u.h"
#include "vm.h"
#include "../alloco/alloco.h"

#include "../loader/bootloader.h"
#include "../loader/vfs.h"
#include "../loader/fat32.h"
#include "../loader/keypad.h"
#include "../loader/ipodhw.h"
#include "../loader/ata2.h"
#include "../loader/interrupts.h"
#include "../loader/fb.h"

extern u16 *framebuffer;

int mlc_printf(const char *fmt, ...);
void mlc_delay_ms (long time_in_ms);
void mlc_clear_screen ();

struct instruction stdlib[][2] =
  { { {.op = OP_AALLOC, .dst = 1, .src1 = 0, .src2 = 0 },
      {.op = OP_RET, .dst = 0, .src1 = 1, .src2 = 0 }},
    { {.op = OP_SYS, .dst = 0, .src1 = SYS_PUTS, .src2 = 0},
      {.op = OP_RET, .dst = 0, .src1 = 0, .src2 = 0 }},
    { {.op = OP_ALEN, .dst = 1, .src1 = 0, .src2 = 0 },
      {.op = OP_RET, .dst = 0, .src1 = 1, .src2 = 0 }},
    { {.op = OP_SYS, .dst = 0, .src1 = SYS_OPEN, .src2 = 0},
      {.op = OP_RET, .dst = 0, .src1 = 0, .src2 = 0 }},
    { {.op = OP_SYS, .dst = 0, .src1 = SYS_CLOSE, .src2 = 0},
      {.op = OP_RET, .dst = 0, .src1 = 0, .src2 = 0 }},
    { {.op = OP_SYS, .dst = 0, .src1 = SYS_LISTD, .src2 = 0},
      {.op = OP_RET, .dst = 0, .src1 = 0, .src2 = 0 }},
    { {.op = OP_SYS, .dst = 0, .src1 = SYS_EXEC, .src2 = 0},
      {.op = OP_RET, .dst = 0, .src1 = 0, .src2 = 0 }},
    { {.op = OP_SYS, .dst = 0, .src1 = SYS_KEYW, .src2 = 0},
      {.op = OP_RET, .dst = 0, .src1 = 0, .src2 = 0 }},
    { {.op = OP_SYS, .dst = 1, .src1 = SYS_OPEND, .src2 = 0},
      {.op = OP_RET, .dst = 0, .src1 = 1, .src2 = 0 }},
    { {.op = OP_SYS, .dst = 0, .src1 = SYS_CLOSED, .src2 = 0},
      {.op = OP_RET, .dst = 0, .src1 = 0, .src2 = 0 }},
    { {.op = OP_SYS, .dst = 0, .src1 = SYS_PWROFF, .src2 = 0},
      {.op = OP_RET, .dst = 0, .src1 = 0, .src2 = 0 }},
    { {.op = OP_SYS, .dst = 0, .src1 = SYS_READF, .src2 = 0},
      {.op = OP_RET, .dst = 0, .src1 = 0, .src2 = 0 }},
    { {.op = OP_SYS, .dst = 0, .src1 = SYS_CLS, .src2 = 0},
      {.op = OP_RET, .dst = 0, .src1 = 0, .src2 = 0 }},
  };

u32 stdlib_n = 13;

void
vm_load_file(char *file, struct vm *vm)
{
	int fd;
	u32 n;

	fd = vfs_open(file);
	vfs_read((void *)&n, 4, 1, fd);
	vm->funs = (void *)malloc((n + stdlib_n) * sizeof(struct instruction *));
	vm->pc = 0;

	vm->files = (void *)malloc(NFILES_MAX * sizeof(void *));
	for (int i = 0; i < NFILES_MAX; ++i)
		vm->files[i] = 0;

	for (int i = 0; i < stdlib_n; ++i)
		vm->funs[i] = &stdlib[i][0];

	for (int i = 0; i < n; ++i) {
		u32 j;
		vfs_read((void *)&j, 4, 1, fd);
		vm->funs[i + stdlib_n] =
		    (void *)malloc(j * sizeof(struct instruction));
		vfs_read((void *)(vm->funs[i + stdlib_n]),
		                  j * sizeof(struct instruction), 1, fd);
	}
	vfs_close(fd);
	vm->code = vm->funs[n + stdlib_n - 1];
}

static i32
load(struct vm *vm, u8 a)
{
	return vm->regs[a];
}

static void
store(struct vm *vm, u8 a, i32 v)
{
	vm->regs[a] = v;
}

static struct instruction *
load_fun (struct vm *vm, u32 f)
{
	return vm->funs[f];
}

static void
sys_puts(struct vm *vm, struct instruction i)
{
	i32 *p = (i32 *)load(vm, i.src2);
	for (int j = 1; j <= p[0]; ++j)
		mlc_printf("%c", (char)p[j]);
}

static void
sys_open(struct vm *vm, struct instruction i)
{
	i32 *p = (i32 *)load(vm, i.src2);
	char f[p[0] + 8];

	f[0] = '('; f[1] = 'h'; f[2] = 'd'; f[3] = '0'; f[4] = ','; f[5] = '1';
	f[6] = ')';
	for (int j = 1; j <= p[0]; ++j)
		f[6 + j] = (char)p[j];
	f[7 + p[0]] = '\0';

	store(vm, i.dst,  vfs_open(f));
}

static void
sys_close(struct vm *vm, struct instruction i)
{
	vfs_close(load(vm, i.src2));
}

static void
sys_key_wait(struct vm *vm, struct instruction i)
{
	int key;

	while ((key = keypad_getkey()) == 0);
	store(vm, i.dst, key);
}

static void
sys_exec(struct vm *vm, struct instruction i)
{
	i32 *p = (i32 *)load(vm, i.src2);
	struct vm nvm;
	char f[p[0] + 8];

	f[0] = '('; f[1] = 'h'; f[2] = 'd'; f[3] = '0'; f[4] = ','; f[5] = '1';
	f[6] = ')';
	for (int j = 1; j <= p[0]; ++j)
		f[6 + j] = (char)p[j];
	f[7 + p[0]] = '\0';

	vm_load_file(f, &nvm);
	store(vm, i.dst, vm_run(&nvm));
}

static void
sys_open_dir(struct vm *vm, struct instruction i)
{
	i32 *p = (i32 *)load(vm, i.src2);
	char d[p[0] + 1];

	for (int i = 0; i < p[0]; ++i)
		d[i] = (char)p[i + 1];
	d[p[0]] = '\0';

	store(vm, i.dst, fat32_open_dir(d, vm));
}

static void
sys_close_dir(struct vm *vm, struct instruction i)
{
	fat32_close_dir(load(vm, i.src2), vm);
}

// todo do cleanly
#define MAXDIRSIZE 128
static void
sys_list_dir(struct vm *vm, struct instruction i)
{
	i32 **p;
	int fd;
	int j, len;
	i32 buf[256];

	p = (i32 **)malloc(sizeof(i32) + MAXDIRSIZE * sizeof(i32 *));
	fd = load(vm, i.src2);
	j = 1;
	while ((len = fat32_next_entry_dir(fd, buf, vm)) >= 0) {
		p[j] = (void *)malloc((len + 1) * sizeof(i32));
		p[j][0] = len;
		for (int k = 1; k <= len; ++k) {
			p[j][k] = buf[k - 1];
		}
		j += 1;
	}

	((i32 *)p)[0] = j - 1;
	store(vm, i.dst, (i32)p);
}

static void
sys_poweroff(struct vm *vm, struct instruction i)
{
	keypad_exit ();
	ata_exit ();
	exit_irqs ();
	ipod_set_backlight (0);
	fb_cls (framebuffer, ipod_get_hwinfo()->lcd_is_grayscale?BLACK:WHITE);
	fb_update(framebuffer);
	mlc_delay_ms (1000);
	pcf_standby_mode ();
}

static void
sys_clear_screen(struct vm *vm, struct instruction i)
{
	mlc_clear_screen ();
	fb_update(framebuffer);
}

static void
sys_read_file(struct vm *vm, struct instruction i)
{
	int fd;
	u32 fs, read;
	i32 *out;
	u8 buf[512];

	fd = load(vm, i.src2);
	vfs_seek(fd, 0, VFS_SEEK_END);
	fs = vfs_tell(fd);
	vfs_seek(fd, 0, VFS_SEEK_SET);
	out = (void *)malloc((1 + fs) * sizeof(i32));
	read = 0;
	while (fs > read) {
		if (fs - read > 512) {
			vfs_read((void *)buf, 512, 1, fd);
			for (int i = 0; i < 512; ++i)
				out[1 + read + i] =  buf[i];
			read += 512;
		} else {
			vfs_read((void *)buf, fs - read, 1, fd);
			for (int i = 0; i < fs - read; ++i)
				out[1 + read + i] =  buf[i];
			read = fs;
		}
	}
	out[0] = fs;
	store(vm, i.dst, (i32)out);
}

void (*sys_calls[])(struct vm *, struct instruction) =
    { [SYS_OPEN] = sys_open,
      [SYS_PUTS] = sys_puts,
      [SYS_EXEC] = sys_exec,
      [SYS_CLOSE] = sys_close,
      [SYS_KEYW] = sys_key_wait,
      [SYS_OPEND] = sys_open_dir,
      [SYS_CLOSED] = sys_close_dir,
      [SYS_LISTD] = sys_list_dir,
      [SYS_PWROFF] = sys_poweroff,
      [SYS_CLS] = sys_clear_screen,
      [SYS_READF] = sys_read_file
    };

i32
vm_run(struct vm *vm)
{
	struct instruction *code;

	code = vm->code;
	vm->pc = 0;

	for (;;) {
		struct instruction i = code[vm->pc];
		switch (i.op) {
		case OP_RET:
			return load(vm, i.src1);
		case OP_ADDI:
			store(vm, i.dst, load(vm, i.src1) + load(vm, i.src2));
			++vm->pc;
			break;
		case OP_SUBI:
			store(vm, i.dst, load(vm, i.src1) - load(vm, i.src2));
			++vm->pc;
			break;
		case OP_MULI:
			store(vm, i.dst, load(vm, i.src1) * load(vm, i.src2));
			++vm->pc;
			break;
		case OP_DIVI:
			store(vm, i.dst, load(vm, i.src1) / load(vm, i.src2));
			++vm->pc;
			break;
		case OP_LOADI: {
			i32 v = ((i32 *)code)[vm->pc + 1];
			store(vm, i.dst, v);
			vm->pc += 2;
			break;
		}
		case OP_EQUI:
			store(vm, i.dst, load(vm, i.src1) == load(vm, i.src2));
			++vm->pc;
			break;
		case OP_GRTI:
			store(vm, i.dst, load(vm, i.src1) > load(vm, i.src2));
			++vm->pc;
			break;
		case OP_GRTEI:
			store(vm, i.dst, load(vm, i.src1) >= load(vm, i.src2));
			++vm->pc;
			break;
		case OP_ANDI:
			store(vm, i.dst, load(vm, i.src1) & load(vm, i.src2));
			++vm->pc;
			break;
		case OP_ORI:
			store(vm, i.dst, load(vm, i.src1) | load(vm, i.src2));
			++vm->pc;
			break;
		case OP_NOTI:
			store(vm, i.dst, ~load(vm, i.src1));
			++vm->pc;
			break;
		case OP_CJMP:
			if (1 & load(vm, i.src1)) {
				vm->pc = ((u32 *)code)[vm->pc + 1];
			} else {
				vm->pc += 2;
			}
			break;
		case OP_JMP:
			vm->pc = ((u32 *)code)[vm->pc + 1];
			break;
		case OP_ASETI: {
			i32 *p = (i32 *)(load(vm, i.dst));
			p[1 + load(vm, i.src1)] = load(vm, i.src2);
			++vm->pc;
			break;
		}
		case OP_AGETI: {
			i32 *p = (i32 *)(load(vm, i.src1));
			store(vm, i.dst, p[1 + load(vm, i.src2)]);
			++vm->pc;
			break;
		}

		case OP_ALEN: {
			i32 *p = (i32 *)(load(vm, i.src1));
			store(vm, i.dst, p[0]);
			++vm->pc;
			break;
		}
		case OP_CALL: {
			struct vm nvm;
			nvm.pc = 0;
			for (int j = 0; j < i.src1; ++j)
				nvm.regs[j] = load(vm, code[vm->pc + 2 + j].src1);
			nvm.files = vm->files;
			nvm.code = load_fun(vm, ((u32 *)code)[vm->pc + 1]);
			nvm.funs = vm->funs;
			store(vm, i.dst, vm_run(&nvm));
			vm->pc += 2 + i.src1;
			break;
		}
		case OP_MOVI:
			store(vm, i.dst, load(vm, i.src1));
			++vm->pc;
			break;
		case OP_AAPPEND: {
			i32 *p1 = (i32 *)(load(vm, i.src1));
			int n1 = p1[0];
			i32 *p2 = (i32 *)(load(vm, i.src2));
			int n2 = p2[0];
			i32 *pd = (i32 *)malloc((n1 + n2 + 1) * sizeof(i32));
			pd[0] = n1 + n2;
			for (int i = 1; i <= n1; ++i)
				pd[i] = p1[i];
			for (int i = 1; i <= n2; ++i)
				pd[n1 + i] = p2[i];
			store(vm, i.dst, (i32)pd);
			++vm->pc;
			break;
		}
		} switch (i.op) {
		case OP_AALLOC: {
			u32 n = load(vm, i.src1);
			i32 *p = (void *)malloc(4 * n);
			store(vm, i.dst, (u32)p);
			p[0] = n;
			++vm->pc;
			break;
		}
		case OP_SYS:
			sys_calls[i.src1](vm, i);
			++vm->pc;
			break;
		}
	}
}
