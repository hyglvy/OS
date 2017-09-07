#ifndef _STDLIB_H
#define _STDLIB_H
#include <sys/defs.h>
int main(int argc, char *argv[], char *envp[]);
void exit(int status);

void *malloc(size_t size);
void free(void *ptr);
void setenv(char *name, char *value, char *envp[]);



#endif
