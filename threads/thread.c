#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "intrinsic.h"
#ifdef USERPROG
#include "userprog/process.h"
#endif

/* Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details. */
#define THREAD_MAGIC 0xcd6abf4b

/* Random value for basic thread
   Do not modify this value. */
#define THREAD_BASIC 0xd42df210

/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. */
static struct list ready_list;

/* Idle thread. */
static struct thread *idle_thread;

/* Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/* Lock used by allocate_tid(). */
static struct lock tid_lock;

/* Thread destruction requests */
static struct list destruction_req;

/* Statistics. */
static long long idle_ticks;    /* # of timer ticks spent idle. */
static long long kernel_ticks;  /* # of timer ticks in kernel threads. */
static long long user_ticks;    /* # of timer ticks in user programs. */

/* Scheduling. */
#define TIME_SLICE 4            /* # of timer ticks to give each thread. */
static unsigned thread_ticks;   /* # of timer ticks since last yield. */

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
bool thread_mlfqs;

// ********************************************** //
// [MOD; SLEEP-WAIT IMPL]
static struct list sleep_list;
// [MOD; MLFQS IMPL]
int load_avg;
static struct list all_list;
// ********************************************** //


static void kernel_thread (thread_func *, void *aux);

static void idle (void *aux UNUSED);
static struct thread *next_thread_to_run (void);
static void init_thread (struct thread *, const char *name, int priority);
static void do_schedule(int status);
static void schedule (void);
static tid_t allocate_tid (void);

/* Returns true if T appears to point to a valid thread. */
#define is_thread(t) ((t) != NULL && (t)->magic == THREAD_MAGIC)

/* Returns the running thread.
 * Read the CPU's stack pointer `rsp', and then round that
 * down to the start of a page.  Since `struct thread' is
 * always at the beginning of a page and the stack pointer is
 * somewhere in the middle, this locates the curent thread. */
#define running_thread() ((struct thread *) (pg_round_down (rrsp ())))

// ********************************************** //
// [MOD; MLFQS IMPL]
#define F (1 << 14)
#define INT_MAX ((1 << 31) - 1)
#define INT_MIN (-(1 << 31))

#define int_to_fp(n) 		(n * F)
#define fp_to_int(x) 		(x / F)
#define fp_to_int_round(x) 	(x >= 0 ? (x + F / 2) / F : (x - F / 2) / F)

#define add_fp(x, y) 		(x + y)
#define sub_fp(x, y) 		(x - y)
#define add_mixed(x, n) 	(x + n * F)
#define sub_mixed(x, n) 	(x - n * F)
#define mult_fp(x, y) 		(((int64_t)x) * y / F)
#define mult_mixed(x, n) 	(x * n)
#define div_fp(x, y) 		(((int64_t)x) * F / y)
#define div_mixed(x, n) 	(x / n)
// ********************************************** //


// Global descriptor table for the thread_start.
// Because the gdt will be setup after the thread_init, we should
// setup temporal gdt first.
static uint64_t gdt[3] = { 0, 0x00af9a000000ffff, 0x00cf92000000ffff };

/* Initializes the threading system by transforming the code
   that's currently running into a thread.  This can't work in
   general and it is possible in this case only because loader.S
   was careful to put the bottom of the stack at a page boundary.

   Also initializes the run queue and the tid lock.

   After calling this function, be sure to initialize the page
   allocator before trying to create any threads with
   thread_create().

   It is not safe to call thread_current() until this function
   finishes. */
void
thread_init (void) {
	ASSERT (intr_get_level () == INTR_OFF);

	/* Reload the temporal gdt for the kernel
	 * This gdt does not include the user context.
	 * The kernel will rebuild the gdt with user context, in gdt_init (). */
	struct desc_ptr gdt_ds = {
		.size = sizeof (gdt) - 1,
		.address = (uint64_t) gdt
	};
	lgdt (&gdt_ds);

	/* Init the global thread context */
	lock_init (&tid_lock);
	list_init (&ready_list);
	list_init (&destruction_req);

	// ********************************************** //
	// [MOD; SLEEP-WAIT IMPL]
	list_init(&sleep_list);
	// [MOD; MLFQS IMPL]
	list_init(&all_list);
	load_avg = 0;
	// ********************************************** //

	/* Set up a thread structure for the running thread. */
	initial_thread = running_thread ();
	init_thread (initial_thread, "main", PRI_DEFAULT);
	initial_thread->status = THREAD_RUNNING;					
	initial_thread->tid = allocate_tid ();
}

/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. */
void
thread_start (void) {
	/* Create the idle thread. */
	struct semaphore idle_started;
	sema_init (&idle_started, 0);
	thread_create ("idle", PRI_MIN, idle, &idle_started);

	// ********************************************** //
	// [MOD; MLFQS IMPL]
	load_avg = LOAD_AVG_DEFAULT;
	// ********************************************** //

	/* Start preemptive thread scheduling. */
	intr_enable ();

	/* Wait for the idle thread to initialize idle_thread. */
	sema_down (&idle_started);
}

/* Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context. */
void
thread_tick (void) {
	struct thread *t = thread_current ();

	/* Update statistics. */
	if (t == idle_thread)
		idle_ticks++;
#ifdef USERPROG
	else if (t->pml4 != NULL)
		user_ticks++;
#endif
	else
		kernel_ticks++;

	/* Enforce preemption. */
	if (++thread_ticks >= TIME_SLICE)
		intr_yield_on_return ();
}

/* Prints thread statistics. */
void
thread_print_stats (void) {
	printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
			idle_ticks, kernel_ticks, user_ticks);
}

/* Creates a new kernel thread named NAME with the given initial
   PRIORITY, which executes FUNCTION passing AUX as the argument,
   and adds it to the ready queue.  Returns the thread identifier
   for the new thread, or TID_ERROR if creation fails.

   If thread_start() has been called, then the new thread may be
   scheduled before thread_create() returns.  It could even exit
   before thread_create() returns.  Contrariwise, the original
   thread may run for any amount of time before the new thread is
   scheduled.  Use a semaphore or some other form of
   synchronization if you need to ensure ordering.

   The code provided sets the new thread's `priority' member to
   PRIORITY, but no actual priority scheduling is implemented.
   Priority scheduling is the goal of Problem 1-3. */
tid_t thread_create (const char *name, int priority, thread_func *function, void *aux) {
	struct thread *t;
	tid_t tid;

	ASSERT (function != NULL);

	/* Allocate thread. */
	t = palloc_get_page (PAL_ZERO);
	if (t == NULL)
		{return TID_ERROR;}

	/* Initialize thread. */
	init_thread (t, name, priority);
	tid = t->tid = allocate_tid ();

		

	/* Call the kernel_thread if it scheduled.
	 * Note) rdi is 1st argument, and rsi is 2nd argument. */
	t->tf.rip = (uintptr_t) kernel_thread;
	t->tf.R.rdi = (uint64_t) function;
	t->tf.R.rsi = (uint64_t) aux;
	t->tf.ds = SEL_KDSEG;
	t->tf.es = SEL_KDSEG;
	t->tf.ss = SEL_KDSEG;
	t->tf.cs = SEL_KCSEG;
	t->tf.eflags = FLAG_IF;
	
		//현재 스레드에 추가 
	list_push_back(&thread_current()->child_list, &t->child_elem);

	//file descripter table을 thread에 추가해준다. 
	t->fdt = palloc_get_multiple(PAL_ZERO, FDT_PAGES);
	if(t->fdt == NULL)
		{return TID_ERROR;}


	/* Add to run queue. */
	thread_unblock (t);
	// ********************************************** //
	// [MOD; MLFQS IMPL]
	if(!thread_mlfqs)
		// ********************************************** //
		// [MOD; PREEMPTION PRIORITY IMPL]
		// DESCRIPTION compare the priorities of the currently running thread and newly created
		// yield the CPU if the newly arrived thread have higher priority
		thread_preemption_priority();
		// ********************************************** //
	// ********************************************** //

	return tid;
}

/* Puts the current thread to sleep.  It will not be scheduled
   again until awoken by thread_unblock().

   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h. */
void
thread_block (void) {
	ASSERT (!intr_context ());
	ASSERT (intr_get_level () == INTR_OFF);
	thread_current ()->status = THREAD_BLOCKED;
	schedule ();
}

/* Transitions a blocked thread T to the ready-to-run state.
   This is an error if T is not blocked.  (Use thread_yield() to
   make the running thread ready.)

   This function does not preempt the running thread.  This can
   be important: if the caller had disabled interrupts itself,
   it may expect that it can atomically unblock a thread and
   update other data. */
void
thread_unblock (struct thread *t) {
	enum intr_level old_level = intr_disable ();

	ASSERT (is_thread (t));
	ASSERT (t->status == THREAD_BLOCKED);

	// ********************************************** //
	// [ MOD; READY_LIST PRIORITY IMPL]
	// DESCRIPTION keeping the ready_list in DESC order
	list_insert_ordered(&ready_list, &t->elem, thread_insert_priority_helper, NULL);
	// ********************************************** //

	// [LEGACY] non priority
	// list_push_back (&ready_list, &t->elem);

	t->status = THREAD_READY; 
	intr_set_level (old_level);
}


// ********************************************** //
// [MOD; READY_LIST PRIORITY IMPL]
// DESCRIPTION checks the priority of two thread (based on elem) and traverses the list
// until the value of the priority is less than the next element (the point of insertion)
// @ void *aux is entirely there for syntax (see list_insert_orderded code for clarification)
bool 
thread_insert_priority_helper(const struct list_elem *curr_elem, const struct list_elem *cmp_elem, void *aux UNUSED) {
	struct thread *curr_thread = list_entry(curr_elem, struct thread, elem);
	struct thread *cmp_thread = list_entry(cmp_elem, struct thread, elem);

	return curr_thread->priority > cmp_thread->priority;
}
// ********************************************** //

// ********************************************** //
// [MOD; DONATION PRIORITY IMPL]
// DESCRIPTION checks the priority of two threads (based on donation_elem) and traverses the list
// until the value of the priority is less than the next element (the point of insertion)
// @ void *aux is entirely there for syntax (see list_insert_orderded code for clarification)
bool 
thread_donation_priority_helper(const struct list_elem *curr_elem, const struct list_elem *cmp_elem, void *aux UNUSED) {
	struct thread *curr_thread = list_entry(curr_elem, struct thread, donation_elem);
	struct thread *cmp_thread = list_entry(cmp_elem, struct thread, donation_elem);

	return curr_thread->priority > cmp_thread->priority;
}
// ********************************************** //

/* Returns the name of the running thread. */
const char *
thread_name (void) {
	return thread_current ()->name;
}

/* Returns the running thread.
   This is running_thread() plus a couple of sanity checks.
   See the big comment at the top of thread.h for details. */
struct thread *
thread_current (void) {
	struct thread *t = running_thread ();

	/* Make sure T is really a thread.
	   If either of these assertions fire, then your thread may
	   have overflowed its stack.  Each thread has less than 4 kB
	   of stack, so a few big automatic arrays or moderate
	   recursion can cause stack overflow. */
	ASSERT (is_thread (t));
	ASSERT (t->status == THREAD_RUNNING);

	return t;
}

/* Returns the running thread's tid. */
tid_t
thread_tid (void) {
	return thread_current ()->tid;
}

/* Deschedules the current thread and destroys it.  Never
   returns to the caller. */
void
thread_exit (void) {
	ASSERT (!intr_context ());

#ifdef USERPROG
	process_exit ();
#endif

	/* Just set our status to dying and schedule another process.
	   We will be destroyed during the call to schedule_tail(). */
	intr_disable ();
	// ********************************************** //
	// [MOD; MLFQS IMPL]
	list_remove(&thread_current()->all_elem);
	// ********************************************** //
	do_schedule (THREAD_DYING);
	NOT_REACHED ();
}

/* Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim. */
void
thread_yield (void) {
	enum intr_level old_level = intr_disable();

	struct thread *curr = thread_current();

	// ********************************************** //
	// [MOD; PREEMPTION PRIORITY IMPL]
	// DESCRIPTION put the current thread into the ready list (in priority order)
	// and pull the current running thread out as ready
	list_insert_ordered(&ready_list, &curr->elem, thread_insert_priority_helper, NULL);
	do_schedule(THREAD_READY);
	// ********************************************** //

	// [LEGACY] BUSY-WAIT POLICY
	// if (curr != idle_thread)
	// 	list_push_back (&ready_list, &curr->elem);
	// do_schedule(THREAD_READY);

	intr_set_level (old_level);
}

// ********************************************** //
// [MOD; PREEMPTION PRIORITY IMPL]
// DESCRIPTION when the current running thread has less priority than the
// first ready_list thread, then it will yield (give up the cpu)
void
thread_preemption_priority(void) {
	enum intr_level old_level = intr_disable();

	// if no thread is running and ready_list is empty (see function idle for clarification)
	if(thread_current() == idle_thread)
		return;

	struct thread *curr_thread = thread_current();
	struct thread *first_ready_thread = list_entry(list_begin(&ready_list), struct thread, elem);
	
	if(first_ready_thread->priority > curr_thread->priority)
		thread_yield();
	
	intr_set_level(old_level);
}
// ********************************************** //

// ********************************************** //
// [MOD; SLEEP-WAIT IMPL]
// DESCRIPTION change the state of caller thread to BLOCKED (insert to sleep_list),
// store the tick when it needs to be woken up
void
thread_sleep(int64_t sleep_ticks) {
	enum intr_level old_level = intr_disable();

	struct thread *curr_thread = thread_current();

	curr_thread->awake_ticks = sleep_ticks;
	list_push_back(&sleep_list, &curr_thread->elem);
	thread_block();

	intr_set_level(old_level);
}
// ********************************************** //

// ********************************************** //
// [MOD; SLEEP-WAIT IMPL]
// DESCRIPTION if there is a thread that can be woken up according to global tick
void
thread_awake(int64_t global_ticks) {
	enum intr_level old_level = intr_disable();

	struct list_elem *curr_elem = list_begin(&sleep_list);
	struct thread *curr_thread;

	// time complexity : O(n)
	while(curr_elem != list_end(&sleep_list)) {
		curr_thread = list_entry(curr_elem, struct thread, elem);

		if(global_ticks >= curr_thread->awake_ticks) {
			curr_elem = list_remove(curr_elem);
			thread_unblock(curr_thread);
		} else
			curr_elem = list_next(curr_elem);
	}

	intr_set_level(old_level);
}
// ********************************************** //

// ********************************************** //
// [MOD; DONATE PRIORITY IMPL]
// DESCRIPTION recurse through the priority and if priority is higher in donation list
// update the priority to the highest priority
void 
thread_insert_donation_priority(struct thread *curr_thread, int depth) {
    if (depth == 0 || curr_thread->wait_lock == NULL)
        return;

    struct thread *lock_holding_thread = curr_thread->wait_lock->holder;
	if(lock_holding_thread->priority < curr_thread->priority)
    	lock_holding_thread->priority = curr_thread->priority;
    thread_insert_donation_priority(lock_holding_thread, depth - 1);
}
// ********************************************** //

// ********************************************** //
// [MOD; DONATE PRIORITY IMPL]
// DESCRIPTION updating the priority based on the current donation list
void 
thread_update_donation_priority(void) {
	struct thread *curr_thread = thread_current();
	struct list *curr_donation_list = &(curr_thread->donation_list);

	if(list_empty(curr_donation_list) || curr_thread->original_priority > list_entry(list_front(curr_donation_list), struct thread, donation_elem)->priority)
		curr_thread->priority = curr_thread->original_priority;
	else
		curr_thread->priority = list_entry(list_front(curr_donation_list), struct thread, donation_elem)->priority;
}
// ********************************************** //

// ********************************************** //
// [MOD; DONATE PRIORITY IMPL]
// DESCRIPTION remove donor from donation list by traversing through
// the entire donation list
void
thread_remove_donor_from_donation_list(struct lock *curr_lock, struct list *curr_donation_list) {
	struct thread *donor_thread;
	struct list_elem *donor_elem = list_front(curr_donation_list);

	// time complexity = O(n)
	while(donor_elem != list_end(curr_donation_list)) {
		donor_thread = list_entry(donor_elem, struct thread, donation_elem);

		if(donor_thread->wait_lock == curr_lock)
			donor_elem = list_remove(donor_elem);
		else
			donor_elem = list_next(donor_elem);
	}
}
// ********************************************** //

/* Sets the current thread's priority to NEW_PRIORITY. */
void
thread_set_priority (int new_priority) {
	// ********************************************** //
	// [MOD; DONATION PRIORITY IMPL]
	// DESCRIPTION in mlfqs will not explicitly set thread priority,
	// this should be done by the scheduler
	if(thread_mlfqs)
		return;
	// ********************************************** //
	
	// ********************************************** //
	// [MOD; DONATION PRIORITY IMPL]
	// DESCRIPTION updating accordingly when priority is modified at some random point
	// when priority is modified at some random point, the original priority must be
	// modified to update donation priority accordingly
	thread_current()->original_priority = new_priority;
	thread_update_donation_priority();
	// ********************************************** //

	// ********************************************** //
	// [MOD; PREEMPTION PRIORITY IMPL]
	// DESCRIPTION updating accordingly when priority is modified at some random point
	thread_preemption_priority();
	// ********************************************** //
}

/* Returns the current thread's priority. */
int
thread_get_priority (void) {
	// ********************************************** //
	// [MOD; MLFQS IMPL]
	return thread_current()->priority;
	// ********************************************** //
}

/* Sets the current thread's nice value to NICE. */
void
thread_set_nice (int nice) {
	// ********************************************** //
	// [MOD; MLFQS IMPL]
	struct thread *curr_thread = thread_current();

	curr_thread->nice = nice;
	thread_mlfqs_calculate_priority();
	thread_preemption_priority();
	// ********************************************** //
}

/* Returns the current thread's nice value. */
int
thread_get_nice (void) {
	// ********************************************** //
	// [MOD; MLFQS IMPL]
	return thread_current()->nice;
	// ********************************************** //
}

/* Returns 100 times the system load average. */
int
thread_get_load_avg (void) {
	// ********************************************** //
	// [MOD; MLFQS IMPL]
	return fp_to_int_round(mult_mixed(load_avg, 100));
	// ********************************************** //
}

/* Returns 100 times the current thread's recent_cpu value. */
int
thread_get_recent_cpu (void) {
	// ********************************************** //
	// [MOD; MLFQS IMPL]
	return fp_to_int_round(mult_mixed(thread_current()->recent_cpu, 100));
	// ********************************************** //
}

// ********************************************** //
// [MOD; MLFQS IMPL]
// DESCRIPTION if a thread is in cpu, increment cpu usage
void
thread_mlfqs_increment_recent_cpu(void) {
	if(thread_current() != idle_thread)
		thread_current()->recent_cpu = add_mixed(thread_current()->recent_cpu, 1);
}
// ********************************************** //

// ********************************************** //
// [MOD; MLFQS IMPL]
// DESCRIPTION calculate load_avg depending on how many threads are asking for cpu
void
thread_mlfqs_calculate_load_avg(void) {
	int curr_no_of_ready_threads;

	if(thread_current() == idle_thread)
		curr_no_of_ready_threads = list_size(&ready_list);
	else
		curr_no_of_ready_threads = list_size(&ready_list) + 1;

	load_avg = add_fp(mult_fp(div_fp(int_to_fp(59), int_to_fp(60)), load_avg),
					mult_mixed(div_fp(int_to_fp(1), int_to_fp(60)), curr_no_of_ready_threads));
}
// ********************************************** //

// ********************************************** //
// [MOD; MLFQS IMPL]
// DESCRIPTION update cpu usage for all threads
void
thread_mlfqs_calculate_recent_cpu(void) {
	struct thread *curr_thread;
	struct list_elem *curr_elem = list_begin(&all_list);

	while(curr_elem != list_end(&all_list)) {
		curr_thread = list_entry(curr_elem, struct thread, all_elem);
		curr_thread->recent_cpu = add_mixed(mult_fp(div_fp(mult_mixed(load_avg, 2), 
									add_mixed(mult_mixed(load_avg, 2), 1)), 
									curr_thread->recent_cpu), 
									curr_thread->nice);

		curr_elem = list_next(curr_elem);
	}
}
// ********************************************** //

// ********************************************** //
// [MOD; MLFQS IMPL]
// DESCRIPTION update priority according to recent cpu, load avg, and niceness
void
thread_mlfqs_calculate_priority(void) {
	struct thread *curr_thread;
	struct list_elem *curr_elem = list_begin(&all_list);

	while(curr_elem != list_end(&all_list)) {
		curr_thread = list_entry(curr_elem, struct thread, all_elem);
		curr_thread->priority = fp_to_int(add_mixed(div_mixed(curr_thread->recent_cpu, -4),
								PRI_MAX - curr_thread->nice * 2));
		
		curr_elem = list_next(curr_elem);
	}
}
// ********************************************** //

/* Idle thread.  Executes when no other thread is ready to run.

   The idle thread is initially put on the ready list by
   thread_start().  It will be scheduled once initially, at which
   point it initializes idle_thread, "up"s the semaphore passed
   to it to enable thread_start() to continue, and immediately
   blocks.  After that, the idle thread never appears in the
   ready list.  It is returned by next_thread_to_run() as a
   special case when the ready list is empty. */
static void
idle (void *idle_started_ UNUSED) {
	struct semaphore *idle_started = idle_started_;

	idle_thread = thread_current ();
	// ********************************************** //
	// [MOD; MLFQS IMPL]
	list_remove(&idle_thread->all_elem);
	// ********************************************** //
	sema_up (idle_started);

	for (;;) {
		/* Let someone else run. */
		intr_disable ();
		thread_block ();

		/* Re-enable interrupts and wait for the next one.

		   The `sti' instruction disables interrupts until the
		   completion of the next instruction, so these two
		   instructions are executed atomically.  This atomicity is
		   important; otherwise, an interrupt could be handled
		   between re-enabling interrupts and waiting for the next
		   one to occur, wasting as much as one clock tick worth of
		   time.

		   See [IA32-v2a] "HLT", [IA32-v2b] "STI", and [IA32-v3a]
		   7.11.1 "HLT Instruction". */
		asm volatile ("sti; hlt" : : : "memory");
	}
}

/* Function used as the basis for a kernel thread. */
static void
kernel_thread (thread_func *function, void *aux) {
	ASSERT (function != NULL);

	intr_enable ();       /* The scheduler runs with interrupts off. */
	function (aux);       /* Execute the thread function. */
	thread_exit ();       /* If function() returns, kill the thread. */
}


/* Does basic initialization of T as a blocked thread named
   NAME. */
static void
init_thread (struct thread *t, const char *name, int priority) {
	ASSERT (t != NULL);
	ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
	ASSERT (name != NULL);

	memset (t, 0, sizeof *t);
	t->status = THREAD_BLOCKED;
	strlcpy (t->name, name, sizeof t->name);
	t->tf.rsp = (uint64_t) t + PGSIZE - sizeof (void *);
	t->priority = priority;
	t->magic = THREAD_MAGIC;

	// ********************************************** //
	// [MOD; DONATION PRIORITY IMPL]
	// DESCRIPTION original priority holder in order to return 
	// if all donators have been processed and initializing donation list
	t->original_priority = priority;
	t->wait_lock = NULL;
	list_init(&(t->donation_list));
	// ********************************************** //
	


	// ********************************************** //
	// [MOD; MLFQS IMPL]
	t->nice = NICE_DEFAULT;
	t->recent_cpu = RECENT_CPU_DEFAULT;
	list_push_back(&all_list, &t->all_elem);
	// ********************************************** //

	//[시스템콜]
	
	//페이지 테이블 초기화
	t->next_fd = 2;
	//child_list 초기화
	list_init(&(t->child_list));

	// sema_init(&t->load_sema, 0);
	// sema_init(&t->exit_sema, 0);
	// sema_init(&t->wait_sema, 0);
	// ********************************************** //
}

/* Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. */
static struct thread *
next_thread_to_run (void) {
	if (list_empty (&ready_list))
		return idle_thread;
	else
		return list_entry (list_pop_front (&ready_list), struct thread, elem);
}

/* Use iretq to launch the thread */
void
do_iret (struct intr_frame *tf) {
	__asm __volatile(
			"movq %0, %%rsp\n"
			"movq 0(%%rsp),%%r15\n"
			"movq 8(%%rsp),%%r14\n"
			"movq 16(%%rsp),%%r13\n"
			"movq 24(%%rsp),%%r12\n"
			"movq 32(%%rsp),%%r11\n"
			"movq 40(%%rsp),%%r10\n"
			"movq 48(%%rsp),%%r9\n"
			"movq 56(%%rsp),%%r8\n"
			"movq 64(%%rsp),%%rsi\n"
			"movq 72(%%rsp),%%rdi\n"
			"movq 80(%%rsp),%%rbp\n"
			"movq 88(%%rsp),%%rdx\n"
			"movq 96(%%rsp),%%rcx\n"
			"movq 104(%%rsp),%%rbx\n"
			"movq 112(%%rsp),%%rax\n"
			"addq $120,%%rsp\n"
			"movw 8(%%rsp),%%ds\n"
			"movw (%%rsp),%%es\n"
			"addq $32, %%rsp\n"
			"iretq"
			: : "g" ((uint64_t) tf) : "memory");
}

/* Switching the thread by activating the new thread's page
   tables, and, if the previous thread is dying, destroying it.

   At this function's invocation, we just switched from thread
   PREV, the new thread is already running, and interrupts are
   still disabled.

   It's not safe to call printf() until the thread switch is
   complete.  In practice that means that printf()s should be
   added at the end of the function. */
static void
thread_launch (struct thread *th) {
	uint64_t tf_cur = (uint64_t) &running_thread ()->tf;
	uint64_t tf = (uint64_t) &th->tf;
	ASSERT (intr_get_level () == INTR_OFF);

	/* The main switching logic.
	 * We first restore the whole execution context into the intr_frame
	 * and then switching to the next thread by calling do_iret.
	 * Note that, we SHOULD NOT use any stack from here
	 * until switching is done. */
	__asm __volatile (
			/* Store registers that will be used. */
			"push %%rax\n"
			"push %%rbx\n"
			"push %%rcx\n"
			/* Fetch input once */
			"movq %0, %%rax\n"
			"movq %1, %%rcx\n"
			"movq %%r15, 0(%%rax)\n"
			"movq %%r14, 8(%%rax)\n"
			"movq %%r13, 16(%%rax)\n"
			"movq %%r12, 24(%%rax)\n"
			"movq %%r11, 32(%%rax)\n"
			"movq %%r10, 40(%%rax)\n"
			"movq %%r9, 48(%%rax)\n"
			"movq %%r8, 56(%%rax)\n"
			"movq %%rsi, 64(%%rax)\n"
			"movq %%rdi, 72(%%rax)\n"
			"movq %%rbp, 80(%%rax)\n"
			"movq %%rdx, 88(%%rax)\n"
			"pop %%rbx\n"              // Saved rcx
			"movq %%rbx, 96(%%rax)\n"
			"pop %%rbx\n"              // Saved rbx
			"movq %%rbx, 104(%%rax)\n"
			"pop %%rbx\n"              // Saved rax
			"movq %%rbx, 112(%%rax)\n"
			"addq $120, %%rax\n"
			"movw %%es, (%%rax)\n"
			"movw %%ds, 8(%%rax)\n"
			"addq $32, %%rax\n"
			"call __next\n"         // read the current rip.
			"__next:\n"
			"pop %%rbx\n"
			"addq $(out_iret -  __next), %%rbx\n"
			"movq %%rbx, 0(%%rax)\n" // rip
			"movw %%cs, 8(%%rax)\n"  // cs
			"pushfq\n"
			"popq %%rbx\n"
			"mov %%rbx, 16(%%rax)\n" // eflags
			"mov %%rsp, 24(%%rax)\n" // rsp
			"movw %%ss, 32(%%rax)\n"
			"mov %%rcx, %%rdi\n"
			"call do_iret\n"
			"out_iret:\n"
			: : "g"(tf_cur), "g" (tf) : "memory"
			);
}

/* Schedules a new process. At entry, interrupts must be off.
 * This function modify current thread's status to status and then
 * finds another thread to run and switches to it.
 * It's not safe to call printf() in the schedule(). */
static void
do_schedule(int status) {
	ASSERT (intr_get_level () == INTR_OFF);
	ASSERT (thread_current()->status == THREAD_RUNNING);
	while (!list_empty (&destruction_req)) {
		struct thread *victim =
			list_entry (list_pop_front (&destruction_req), struct thread, elem);
		palloc_free_page(victim);
	}
	thread_current ()->status = status;
	schedule ();
}

static void
schedule (void) {
	struct thread *curr = running_thread ();
	struct thread *next = next_thread_to_run ();

	ASSERT (intr_get_level () == INTR_OFF);
	ASSERT (curr->status != THREAD_RUNNING);
	ASSERT (is_thread (next));
	/* Mark us as running. */
	next->status = THREAD_RUNNING;

	/* Start new time slice. */
	thread_ticks = 0;

#ifdef USERPROG
	/* Activate the new address space. */
	process_activate (next);
#endif

	if (curr != next) {
		/* If the thread we switched from is dying, destroy its struct
		   thread. This must happen late so that thread_exit() doesn't
		   pull out the rug under itself.
		   We just queuing the page free reqeust here because the page is
		   currently used by the stack.
		   The real destruction logic will be called at the beginning of the
		   schedule(). */
		if (curr && curr->status == THREAD_DYING && curr != initial_thread) {
			ASSERT (curr != next);
			list_push_back (&destruction_req, &curr->elem);
		}

		/* Before switching the thread, we first save the information
		 * of current running. */
		thread_launch (next);
	}
}

/* Returns a tid to use for a new thread. */
static tid_t
allocate_tid (void) {
	static tid_t next_tid = 1;
	tid_t tid;

	lock_acquire (&tid_lock);
	tid = next_tid++;
	lock_release (&tid_lock);

	return tid;
}
