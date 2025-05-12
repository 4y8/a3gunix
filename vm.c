#include "vm.h"

#include "../loader/bootloader.h"
#include "../loader/vfs.h"

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
	mlc_printf("\n");
}

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
		//} switch (i.op) {
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
		//} switch (i.op) {
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
		//} switch (i.op) {
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
			if (i.src1 == SYS_PUTS) {
				sys_puts(vm, i);
			}
			++vm->pc;
			break;
		}
	}
}

struct instruction stdlib[][2] = 
  { {}, 
    { {.op = OP_SYS, .dst = 0, .src1 = SYS_PUTS, .src2 = 0},
      {.op = OP_RET, .dst = 0, .src1 = 0, .src2 = 0}} };

u32 stdlib_n = 2;


void
vm_load_file(char *file, struct vm *vm)
{
	int fd;
	u32 n;

	fd = vfs_open(file);
	vfs_read((void *)&n, 4, 1, fd);
	vm->funs = (void *)malloc(n * sizeof(struct instruction *));
	vm->pc = 0;

	for (int i = 0; i < stdlib_n; ++i) {
		vm->funs[i] = &stdlib[i][0];
	}
	for (int i = 0; i < n; ++i) {
		u32 j;
		vfs_read((void *)&j, 4, 1, fd);
		vm->funs[i + stdlib_n] = (void *)malloc(j * sizeof(struct instruction));
		vfs_read((void *)(vm->funs[i + stdlib_n]),
		                  j * sizeof(struct instruction), 1, fd);
	}
	vfs_close(fd);
	vm->code = vm->funs[n + stdlib_n - 1];
}
