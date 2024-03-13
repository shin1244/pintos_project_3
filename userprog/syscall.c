#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *);

void check_address(void *addr);

/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081         /* Segment selector msr */
#define MSR_LSTAR 0xc0000082        /* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

int exec(const char*cmp_line);



void
syscall_init (void) {
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
			((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t) syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);

	/*No internal synchronization. Concurrent accesses will interfere with one another.
	You should use synchronization to ensure that only one process at a time is executing file system code.
	이 특징에 따라 file에서 가져온 것들을 한번에 하나씩 사용하기 위해서 
	init에 넣어줌. */
	lock_init(&filesys_lock);
}

/*user 프로그램이 잘못된 포인터를 전달할시 exit 역할을 하는 함수
  포인터가 전달된 시스템 콜이 사용됐을때 검중된 경우에 사용*/
void check_address(void *addr)
{
	if(addr == NULL)
		exit(-1);
	if(!is_user_vaddr(addr))
		exit(-1);
	if(pml4_get_page(thread_current()->pml4, addr) == NULL)
		exit(-1);
}

int exec(const char*cmd_line)
{
	check_address(cmd_line);

	char *cmd_line_copy;
	cmd_line_copy = palloc_get_page(0);
	if(cmd_line_copy == NULL)
	 	exit(-1);
	strlcpy(cmd_line_copy, cmd_line, PGSIZE);
}



/* The main system call interface */
void
syscall_handler (struct intr_frame *f UNUSED) {
	// TODO: Your implementation goes here.
	switch (f->R.rax)
	{
	case SYS_EXEC:
		f->R.rax = exec(f->R.rdi);
		break;

	case SYS_OPEN:
		f->R.rax = open(f->R.rdi);
		break;
	
	default:
		break;
	}
	printf ("system call!\n");
	thread_exit ();
}
