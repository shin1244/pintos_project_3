#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"

tid_t process_create_initd (const char *file_name);
tid_t process_fork (const char *name, struct intr_frame *if_);
int process_exec (void *f_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (struct thread *next);

void argument_stack(char **parse, int count, void **rsp);

//현재 스레드에 fdt 파일을 추가해준다. 
int process_add_file(struct file *f);

//__process_fork에서 사용하는 인자를 받아서 쓰는 거 새로 선언
struct thread *get_child_process(int tid);


#endif /* userprog/process.h */
