#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <debug.h>
#include <list.h>
#include <stdint.h>
#include "threads/interrupt.h"
#include "threads/synch.h"
#ifdef VM
#include "vm/vm.h"
#endif


/* States in a thread's life cycle. */
enum thread_status {
	THREAD_RUNNING,     /* Running thread. */
	THREAD_READY,       /* Not running but ready to run. */
	THREAD_BLOCKED,     /* Waiting for an event to trigger. */
	THREAD_DYING        /* About to be destroyed. */
};

/* Thread identifier type.
   You can redefine this to whatever type you like. */
typedef int tid_t;
#define TID_ERROR ((tid_t) -1)          /* Error value for tid_t. */

/* Thread priorities. */
#define PRI_MIN 0                       /* Lowest priority. */
#define PRI_DEFAULT 31                  /* Default priority. */
#define PRI_MAX 63                      /* Highest priority. */

// ********************************************** //
// [MOD; MLFQS IMPL]
#define NICE_MIN			-20
#define NICE_MAX			20
#define NICE_DEFAULT		0
#define RECENT_CPU_DEFAULT	0
#define LOAD_AVG_DEFAULT	0
// ********************************************** //

//**//
//file disripter
// #define FDT_PAGES 2
#define FDT_COUNT_LIMIT 128

//**//
void check_address(void *addr);
void halt(void);
void exit(int status);
// tid_t fork(const char*thread_name, struct intr_frame*f);
int exec(const char *command_line);
// int wait(int pid);
bool create(const char *file, unsigned inital_size);
bool remove(const char *file);
int open(const char *file_name);
int filesize(int fd);
int read(int fd, void *buffer, unsigned size);
int write(int fd, const void *buffer, unsigned size);
void seek(int fd, unsigned position);
unsigned tell(int fd);
void close (int fd);

/* A kernel thread or user process.
 *
 * Each thread structure is stored in its own 4 kB page.  The
 * thread structure itself sits at the very bottom of the page
 * (at offset 0).  The rest of the page is reserved for the
 * thread's kernel stack, which grows downward from the top of
 * the page (at offset 4 kB).  Here's an illustration:
 *
 *      4 kB +---------------------------------+
 *           |          kernel stack           |
 *           |                |                |
 *           |                |                |
 *           |                V                |
 *           |         grows downward          |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           +---------------------------------+
 *           |              magic              |
 *           |            intr_frame           |
 *           |                :                |
 *           |                :                |
 *           |               name              |
 *           |              status             |
 *      0 kB +---------------------------------+
 *
 * The upshot of this is twofold:
 *
 *    1. First, `struct thread' must not be allowed to grow too
 *       big.  If it does, then there will not be enough room for
 *       the kernel stack.  Our base `struct thread' is only a
 *       few bytes in size.  It probably should stay well under 1
 *       kB.
 *
 *    2. Second, kernel stacks must not be allowed to grow too
 *       large.  If a stack overflows, it will corrupt the thread
 *       state.  Thus, kernel functions should not allocate large
 *       structures or arrays as non-static local variables.  Use
 *       dynamic allocation with malloc() or palloc_get_page()
 *       instead.
 *
 * The first symptom of either of these problems will probably be
 * an assertion failure in thread_current(), which checks that
 * the `magic' member of the running thread's `struct thread' is
 * set to THREAD_MAGIC.  Stack overflow will normally change this
 * value, triggering the assertion. */
/* The `elem' member has a dual purpose.  It can be an element in
 * the run queue (thread.c), or it can be an element in a
 * semaphore wait list (synch.c).  It can be used these two ways
 * only because they are mutually exclusive: only a thread in the
 * ready state is on the run queue, whereas only a thread in the
 * blocked state is on a semaphore wait list. */
struct thread {
	/* Owned by thread.c. */
	tid_t tid;                          /* Thread identifier. */
	enum thread_status status;          /* Thread state. */
	char name[16];                      /* Name (for debugging purposes). */
	int priority;                       /* Priority. */

	// ********************************************** //
	// [MOD; SLEEP-WAIT IMPL]
	int64_t awake_ticks;
	/* Shared between thread.c and synch.c. */
	struct list_elem elem;              /* List element. */
	// [MOD; DONATION PRIORITY IMPL]
	int original_priority;
	struct lock *wait_lock;
	struct list donation_list;
	struct list_elem donation_elem;
	// [MOD; MLFQS IMPL]
	int nice;
	int recent_cpu;
	struct list_elem all_elem;

	//[MOD; syscall]
	// ********************************************** //
	int exit_status; //1을 제외한 나머지는 
	struct file**fdt;
	int next_fd;


	struct intr_frame parent_if;
	//자식 list와 elem추가
	struct list child_list;
	struct list_elem child_elem;


	struct semaphore load_sema; // 현재 스레드가 load되는 동안 부모가 기다리게 하기 위한 semaphore
	struct semaphore exit_sema;
	struct semaphore wait_sema;

	struct file *running; //실행중인 파일을 저장


#ifdef USERPROG
	/* Owned by userprog/process.c. */
	uint64_t *pml4;                     /* Page map level 4 */
#endif
#ifdef VM
	/* Table for whole virtual memory owned by thread. */
	struct supplemental_page_table spt;
#endif

	/* Owned by thread.c. */
	struct intr_frame tf;               /* Information for switching */
	unsigned magic;                     /* Detects stack overflow. */
};

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
extern bool thread_mlfqs;

void thread_init (void);
void thread_start (void);

void thread_tick (void);
void thread_print_stats (void);

typedef void thread_func (void *aux);
tid_t thread_create (const char *name, int priority, thread_func *, void *);

void thread_block (void);
void thread_unblock (struct thread *);

struct thread *thread_current (void);
tid_t thread_tid (void);
const char *thread_name (void);

void thread_exit (void) NO_RETURN;
void thread_yield (void);

// ********************************************** //
// [MOD; SLEEP-WAIT IMPL]
void thread_sleep(int64_t sleep_ticks);
void thread_awake(int64_t global_ticks);
// [MOD; READY_LIST PRIORITY IMPL]
bool thread_insert_priority_helper(const struct list_elem *elem_1, const struct list_elem *elem_2, void *aux);
// [MOD; PREEMPTION PRIORITY IMPL]
void thread_preemption_priority(void);
// [MOD; DONATE PRIORITY IMPL]
void thread_insert_donation_priority(struct thread *curr_thread, int depth);
void thread_update_donation_priority(void);
void thread_remove_donor_from_donation_list(struct lock *curr_lock, struct list *curr_donation_list);
bool thread_donation_priority_helper(const struct list_elem *curr_elem, const struct list_elem *cmp_elem, void *aux);
// [MOD; MLFQS IMPL]
void thread_mlfqs_calculate_priority(void);
void thread_mlfqs_increment_recent_cpu(void);
void thread_mlfqs_calculate_recent_cpu(void);
void thread_mlfqs_calculate_load_avg(void);
// ********************************************** //

int thread_get_priority (void);
void thread_set_priority (int);

int thread_get_nice (void);
void thread_set_nice (int);
int thread_get_recent_cpu (void);
int thread_get_load_avg (void);

void do_iret (struct intr_frame *tf);

#endif /* threads/thread.h */
