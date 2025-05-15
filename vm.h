#ifndef __VM_H
#define __VM_H

#define NFILES_MAX 10

enum op {
	OP_RET,
	OP_ADDI, OP_SUBI, OP_MULI, OP_DIVI,
	OP_LOADI,
	OP_EQUI, OP_GRTI, OP_GRTEI,
	OP_ANDI, OP_ORI, OP_NOTI,
	OP_MOVI,
	OP_CJMP, OP_JMP,
	OP_ASETI, OP_AGETI, OP_AALLOC, OP_ALEN,
	OP_CALL,
	OP_SYS, OP_AAPPEND
};

enum syscall {
	SYS_OPEN, SYS_CLOSE, SYS_READF,
	SYS_OPEND, SYS_CLOSED, SYS_LISTD,
	SYS_TYPE,
	SYS_PUTS, SYS_KEYW,
	SYS_EXEC,
	SYS_CLS, SYS_PWROFF
};

struct instruction {
	u8 op;
	u8 dst, src1, src2;
};

struct vm {
	struct instruction *code;
	struct instruction **funs;
	void **files;
	i32 regs[256];
	u32 pc;
};

i32 vm_run(struct vm *vm);

void vm_load_file(char *file, struct vm *vm);

#endif
