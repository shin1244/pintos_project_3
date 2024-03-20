#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H
#include "threads/synch.h"

void syscall_init (void);
void check_address(void *addr);
void halt(void);
void exit(int status);
// tid_t fork(const char*thread_name, struct intr_frame*f);
int exec(const char *command_line);
int wait(int pid);
bool create(const char *file, unsigned inital_size);
bool remove(const char *file);
int open(const char *file);
int filesize(int fd);
int read(int fd, void *buffer, unsigned size);
int write(int fd, const void *buffer, unsigned size);
void seek(int fd, unsigned position);
unsigned tell(int fd);
// void close (int fd);




struct lock filesys_lock;

#endif /* userprog/syscall.h */
