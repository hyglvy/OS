#include <sys/defs.h>
#include <sys/kprintf.h>
#include <sys/tarfs.h>
#include <sys/idt.h>
#include <sys/kernel_threads.h>
#include <sys/kmalloc.h>
#include <sys/elf64.h>
#include <sys/paging.h>
#include <sys/kmemcpy.h>
#include <sys/scheduler.h>
static Task *run_queue;;
static Task *queue_head;;
static Task *schedulerTask;

void setupTask(Task *task, void (*main)(), Task *otherTask) {
    task->regs.rax = 0;
    task->regs.rbx = 0;
    task->regs.rcx = 0;
    task->regs.rdx = 0;
    task->regs.rsi = 0;
    task->regs.rdi = 0;
    task->regs.rflags = otherTask->regs.rflags;
    task->regs.rip = (uint64_t) main;
    task->regs.cr3 = otherTask->regs.cr3;
    task->regs.r8 = 0;
    task->regs.r9 = 0;
    task->regs.r10 = 0;
    task->regs.r11 = 0;
    task->regs.r12 = 0;
    task->regs.r13 = 0;
    task->regs.r14 = 0;
    task->regs.r15 = 0;
    task->regs.rsp = (uint64_t) (4088 + get_free_page(SUPERVISOR_ONLY, task->regs.cr3)); // since it grows downward
    task->kstack = (uint64_t *)task->regs.rsp;//(4088 + get_free_page(SUPERVISOR_ONLY, task->regs.cr3)); // since it grows downward
}

void switch_user_mode(uint64_t symbol) {
        uint64_t oldcr3;
        __asm__ __volatile__("movq %%cr3, %0\n\t"
                             :"=a"(oldcr3));
        kprintf("\ncr3val:%x\n ", oldcr3 );
        __asm__ __volatile__ ( "cli\n\t"
                        "movq %1, %%rsp\n\t"
                        "movw $0x23, %%ax\n\t"
                        "movw %%ax, %%ds\n\t"
                        "movw %%ax, %%es\n\t"
                        "movw %%ax, %%fs\n\t"
                        "movw %%ax, %%gs\n\t"
                        "movq %%rsp, %%rax\n\t"
                        "pushq $0x23\n\t"
                        "pushq %%rax\n\t"
                        "pushfq\n\t"
                        "popq %%rax\n\t"
                        "orq $0x200, %%rax\n\t"
                        "pushq %%rax\n\t"
                        "pushq $0x2B\n\t"
                        "push %0\n\t"
                        "iretq\n\t"
                        ::"b"(symbol), "c"(CURRENT_TASK->mm->stack_begin)
        );
}

void put_in_run_queue(Task *newtask) {
	run_queue->next = newtask;
	newtask->next = queue_head;
	run_queue = newtask;
	return;
}

void schedule(){
//	Task * curr = CURRENT_TASK;
//	Task * next = run_queue;
	

}
void idle_task() {
	kprintf("In the idle task, will stay here forever unless a new thread is available to schedule\n");
	while(1) {
		schedule();
		__asm__ __volatile__("hlt\n\t");
	}
}

void bin_init_user() {
	kprintf("bin init kernel thread\n");
//	while(1);
	switch_user_mode(CURRENT_TASK->mm->e_entry);	
}

void init_scheduler() {
	schedulerTask = CURRENT_TASK;
	last_assn_pid = 0;
	run_queue = NULL;

	Task *idleTask = (Task *)kmalloc(sizeof(Task));
	setupTask(idleTask,idle_task,schedulerTask);
	idleTask->pid = (last_assn_pid+1)%MAX_PROC;
	last_assn_pid = idleTask->pid;
	//put_in_run_queue(idleTask);
	run_queue = idleTask;
	queue_head = idleTask;
	run_queue->next = queue_head;
	
	Task *binInit = loadElf("bin/init");
	if(binInit == NULL) {
		kprintf("Could not find the file to load from elf!\n");
		//kernel panic should happen
	}
	setupTask(binInit,bin_init_user,idleTask);
	binInit->regs.cr3 = binInit->mm->pg_pml4;
	put_in_run_queue(binInit);
}


void scheduler() {
	kprintf("Welcome to scheduler, everything will be scheduled tomorrow.\n");
	init_scheduler();
	CURRENT_TASK = run_queue;
	switchTask(&schedulerTask->regs, &CURRENT_TASK->regs);		
	while(1);
}


/*
uint64_t execve(char *binary, char *argv[], char *envp[]) {
        // returns a new task struct with newcr3
        Task *newtask = loadElf(binary);
        uint64_t oldcr3;
        __asm__ __volatile__("movq %%cr3, %0\n\t"
                             :"=a"(oldcr3));
        // switch to new cr3
        __asm__ __volatile__("movq %0, %%cr3\n\t"
                             ::"a"(newtask->regs.cr3));
        // free all old pages
        free_old_page_tables(oldcr3+0xffffffff80000000);
        return 0;
}
*/
