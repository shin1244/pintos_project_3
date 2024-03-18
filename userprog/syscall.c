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
void halt(void);
void exit(int status);
// tid_t fork(const char*thread_name, struct intr_frame*f);
int exec(const char *command_line);
int wait(int pid);
bool create(const char *file, unsigned inital_size);
bool remove(const char *file);
int open(const char *file_name);
int filesize(int fd);
int read(int fd, void *buffer, unsigned size);
int write(int fd, const void *buffer, unsigned size);
void seek(int fd, unsigned position);
unsigned tell(int fd);
// void close (int fd);


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
/* The main system call interface */
void
syscall_handler (struct intr_frame *f UNUSED) {
	// TODO: Your implementation goes here.
	/*
	1번인자 rdi
	2번인자 rsi
	3번인자 rdx
	4번인자 r10
	5번인자 r8
	6번인자 r9
	*/

	switch (f->R.rax)
	{
	case SYS_HALT:
		halt();
		break;
	case SYS_EXIT:
		exit(f->R.rdi);
		break;
	// case SYS_FORK:
	// 	f->R.rax = fork(f->R.rdi, f);
	// 	break;
	case SYS_EXEC:
		f->R.rax = exec(f->R.rdi);
		break;
	// case SYS_WAIT:
	// 	f->R.rax = wait(f->R.rdi);
	// 	break;
	case SYS_CREATE:
		f->R.rax = create(f->R.rdi, f->R.rsi);
		break;
	case SYS_REMOVE:
		f->R.rax = remove(f->R.rdi);
		break;
	case SYS_OPEN:
		f->R.rax = open(f->R.rdi);
		break;
	case SYS_FILESIZE:
		f->R.rax = filesize(f->R.rdi);
		break;
	case SYS_READ:
		f->R.rax = read(f->R.rdi, f->R.rsi, f->R.rdx);
		break;
	case SYS_WRITE:
		f->R.rax = write(f->R.rdi, f->R.rsi, f->R.rdx);
		break;
	case SYS_SEEK:
		seek(f->R.rdi, f->R.rsi);
		break;
	case SYS_TELL:
		f->R.rax = tell(f->R.rdi);
		break;
	// case SYS_CLOSE:
	// 	close(f->R.rdi);
	// 	break;
	// case SYS_MMAP:
	// 	f->R.rax = mmap(f->R.rdi, f->R.rsi, f->R.rdx, f->R.r10, f->R.r8);
	// 	break;
	// case SYS_MUNMAP:
	// 	munmap(f->R.rdi);
	// 	break;
	}

}

/*user 프로그램이 잘못된 포인터를 전달할시 exit 역할을 하는 함수
  포인터가 전달된 시스템 콜이 사용됐을때 검중된 경우에 사용*/

/*새로 작성한 코드*/
void check_address(void *addr)
{
	if (addr == NULL)
		{exit(-1);}
	if(!is_user_vaddr(addr))
		{exit(-1);}
	if(pml4_get_page(thread_current()->pml4, addr) == NULL)
		{exit(-1);}
}

/*시스템콜 모음zip*/

void halt(void)
{
	power_off();
}

void exit(int status)
{
	struct thread *curr = thread_current();
	curr->exit_status = status;
	//스레드 종료시 메세지 출력
	printf("%s: exit(%d)\n", curr->name, status);
	thread_exit();
}

// tid_t fork(const char*thread_name, struct intr_frame*f)
// {
// 	return process_fork(thread_name, f);
// }

/*exec 함수에서는 전달받은 인자를 그냥 받지 않는데 
그 이유는 인자를 파싱하는 과정이 있기 때문에 복사본을 
만들어서 전달한다.*/
int exec(const char *command_line)
{
	check_address(command_line);

	/*새로운 스레드를 만드는 것은 fork가 하는 일이고
	  process_exec 함수안에서 filename을 변경해야하기 때문에 
	  커널 메모리 공간에 커멘드 카피를 만든다.  */
	char *command_line_copy;
	command_line_copy = palloc_get_page(0);
	if(command_line_copy ==  NULL)
		{exit(-1);}
	strlcpy(command_line_copy, command_line, PGSIZE); //위에 값을 복사값에 복사.

	if(process_exec(command_line_copy) == -1)
		{exit(-1);}
}

// int wait(int pid)
// {
// 	return process_wait(pid);
// }

bool create(const char *file, unsigned inital_size)
{
	lock_acquire(&filesys_lock);
	check_address(file);
	bool success = filesys_create(file, inital_size);
	lock_release(&filesys_lock);
	return success;
}

bool remove(const char *file)
{
	check_address(file);
	return filesys_remove(file);
}

int open(const char *file_name)
{
	check_address(file_name);
	struct file *file = filesys_open(file_name);
	if (file == NULL)
		{return -1;}
	int fd = process_add_file(file);
	if (fd == -1)
		{file_close(file);}
	return fd;
}

int filesize(int fd)
{
	struct file *file = process_get_file(fd);
	if(file == NULL)
		{return -1;}
	return file_length(file);
}



int write(int fd, const void *buffer, unsigned size)
{
	check_address(buffer);

	int bytes_write = 0;

	if (fd == STDOUT_FILENO)
	{
		putbuf(buffer, size);
		bytes_write = size;
	}
	else
	{
		if (fd < 2)
			return -1;
		struct file *file = process_get_file(fd);
		if (file == NULL)
			return -1;
		lock_acquire(&filesys_lock);
		bytes_write = file_write(file, buffer, size);
		lock_release(&filesys_lock);
	}
	return bytes_write;

}

void seek(int fd, unsigned position)
{
	struct file *file = process_get_file(fd);
	if (file == NULL)
		{return;}
	file_seek(file, position);
}

unsigned tell(int fd)
{
	struct file *file = process_get_file(fd);
	if(file == NULL)
		{return;}
	return file_tell(file);
}

// void close(int fd)
// {
// 	struct file *file = process_get_file(fd);
// 	if (file == NULL)
// 		return;
// 	file_close(file);
// 	process_close_file(fd);
// }

int read(int fd, void *buffer, unsigned size)
{
	check_address(buffer);

	char *ptr = (char *)buffer;
	int bytes_read = 0;

	lock_acquire(&filesys_lock);
	if (fd == STDIN_FILENO)
	{
		for (int i = 0; i < size; i++)
		{
			*ptr++ = input_getc();
			bytes_read++;
		}
		lock_release(&filesys_lock);
	}
	else
	{
		if (fd < 2)
		{

			lock_release(&filesys_lock);
			return -1;
		}
		struct file *file = process_get_file(fd);
		if (file == NULL)
		{

			lock_release(&filesys_lock);
			return -1;
		}
		bytes_read = file_read(file, buffer, size);
		lock_release(&filesys_lock);
	}
	return bytes_read;
}
