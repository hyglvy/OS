#include <sys/defs.h>
#include <sys/gdt.h>
#include <sys/kprintf.h>
#include <sys/tarfs.h>
#include <sys/ahci.h>
#include <sys/idt.h>
#include <sys/pic.h>
#include <sys/kernel_threads.h>
#include <sys/kmalloc.h>
#include <sys/elf64.h>
/*
deleted pci.c and pci.h due to reduction in available memory
#include <sys/pci.h>
*/
#include <sys/paging.h>
#include <sys/kmemcpy.h>

#define INITIAL_STACK_SIZE 4096
uint8_t initial_stack[INITIAL_STACK_SIZE]__attribute__((aligned(16)));
uint32_t* loader_stack;
extern char kernmem, physbase;
//pg_desc_t *free_list_head;

Task *runningTask;
uint64_t createTable() {
  uint64_t *newPML4 = (uint64_t*) get_physical_free_page();
  uint64_t *temp = (uint64_t *)((uint64_t) newPML4 + (uint64_t)0xffffffff80000000 );
  for(int i = 0; i < 512; i++) {
    temp[i] = PML4[i];
  }
  return (uint64_t) newPML4;
}

void user_mode() {
	__asm__("int $0x80\n\t");
	//kprintf("hi\n");
	while(1);
}

void yield() {
    Task *last = runningTask;
    runningTask = runningTask->next;
    switchTask(&last->regs, &runningTask->regs);
}

void kern_thd() {
  loadElf("bin/sbush"); 
  yield();
}

void createTask(Task *task, void (*main)(), Task *otherTask) {
    task->regs.rax = 0;
    task->regs.rbx = 0;
    task->regs.rcx = 0;
    task->regs.rdx = 0;
    task->regs.rsi = 0;
    task->regs.rdi = 0;
    task->regs.rflags = otherTask->regs.rflags;
    task->regs.rip = (uint64_t) kern_thd;
    task->regs.cr3 = createTable();
    task->regs.r8 = 0;
    task->regs.r9 = 0;
    task->regs.r10 = 0;
    task->regs.r11 = 0;
    task->regs.r12 = 0;
    task->regs.r13 = 0;
    task->regs.r14 = 0;
    task->regs.r15 = 0;
    task->regs.rsp = (uint64_t)kstack; // since it grows downward
}

void initTasking(Task *mainTask, Task *otherTask) {
	__asm__ __volatile__("movq %%cr3, %0\n\t"
                    	     :"=a"(mainTask->regs.cr3));
	__asm__ __volatile__("PUSHFQ \n\t"
			     "movq (%%rsp), %%rax\n\t"
			     "movq %%rax, %0\n\t"
			     "POPFQ\n\t"
			     :"=m"(mainTask->regs.rflags)::"%rax");
	
	createTask(otherTask, kern_thd, mainTask);
    	mainTask->next = otherTask;
    	otherTask->next = mainTask;
 
    	runningTask = mainTask;
}

void start(uint32_t *modulep, void *physbase, void *physfree)
{
  struct smap_t {
    uint64_t base, length;
    uint32_t type;
  }__attribute__((packed)) *smap;

//  int num_pages = 0;
  smap_copy_t smap_copy[10];
  int smap_copy_index = 0;

  while(modulep[0] != 0x9001) modulep += modulep[1]+2;
  for(smap = (struct smap_t*)(modulep+2); smap < (struct smap_t*)((char*)modulep+modulep[1]+2*4); ++smap) {
    if (smap->type == 1 /* memory */ && smap->length != 0) {
      smap_copy[smap_copy_index].starting_addr = smap->base;
      smap_copy[smap_copy_index].last_addr = smap->base + smap->length;
      //kprintf("Available Physical Memory [%p-%p]\n",  smap_copy[smap_copy_index].starting_addr, smap_copy[smap_copy_index].last_addr );
      smap_copy_index++;
    }
  }
  kprintf("tarfs in [%p:%p]\n", &_binary_tarfs_start, &_binary_tarfs_end);

  kprintf("value of PML %x &PML %x and PML[511] %x\n",PML4,&PML4,PML4[511]);
  uint64_t free_list_end = setup_memory(physbase, physfree, smap_copy, smap_copy_index);
  /*remap the 640K-1M region with direct one to one mapping from virtual to physical*/
  uint64_t virt_addr = (uint64_t) 0xffffffff80000000 + (uint64_t)physbase;
  uint64_t PDEframe = (virt_addr >> 21) & (uint64_t) 0x1ff;  
  init_self_referencing(free_list_end, PDEframe);
  
  map_memory_range(0x00000, (uint64_t)physbase, 0); 
  map_memory_range((uint64_t)physbase, free_list_end + (512*511*4096), PDEframe);

  for(int i = 0; i < 512; i++) {
    PDE[i] = PDE[i] | 7;
  }

  uint64_t cr3val = free_list_end;
  __asm __volatile("movq %0, %%cr3\n\t"
                    :
                    :"a"(cr3val));
  uint64_t t1 = (uint64_t)PML4;
  uint64_t temp = (uint64_t)t1 + (uint64_t)0xffffffff80000000;
  PML4 = (uint64_t *) temp;
  t1 = (uint64_t)free_list;
  temp = (uint64_t)t1 + (uint64_t)0xffffffff80000000;
  free_list = (pg_desc_t *) temp;
  uint64_t * temp1 = (uint64_t *)  get_free_page(7);
  temp1[0] = 777;
  free_page(temp1);
  temp1 = (uint64_t *)  get_free_page(7);
  temp1[0] = 7877;
  //-------------k malloc init----------------------
  init_kmalloc(); 

  // ------------------------------------------------
  // switch to user mode
  init_idt();
  //program_pic();
  set_tss_rsp((void *)((uint64_t)kstack));
  // find a page and copy the function to it
  //uint64_t *user_user_mode = (uint64_t *)get_free_page(7);
  //memcpy((char *)user_user_mode, (char *)&user_mode, 8192);
  // now switch to new page
  //switch_user_mode((uint64_t)&user_mode);
  // ------------------------------------------------
  run_queue = NULL;
  last_assn_pid = 0;
 
  Task *mainTask = (Task*) kmalloc(sizeof(Task));
  Task *otherTask = (Task*) kmalloc(sizeof(Task));
  initTasking(mainTask, otherTask);
  kprintf("Trying multitasking from main\n");
  yield();
  kprintf("back in main for the last time\n");
  // ------------------------------------------------
  //__asm__ __volatile("sti");
  //kprintf("physfree %p physbase %p\n", (uint64_t)physfree, (uint64_t)physbase);
  //hba_port_t* port = enumerate_pci();
  //if (port == NULL) kprintf("nothing found\n");
  while(1);
}

void boot(void)
{
  // note: function changes rsp, local stack variables can't be practically used
  register char /**temp1,*/ *temp2;

  for(temp2 = (char*)0xb8001; temp2 < (char*)0xb8000+160*25; temp2 += 2) *temp2 = 7 /* white */;
  __asm__ volatile (
    "cli;"
    "movq %%rsp, %0;"
    "movq %1, %%rsp;"
    :"=g"(loader_stack)
    :"r"(&initial_stack[INITIAL_STACK_SIZE])
  );
  init_gdt();
  start(
    (uint32_t*)((char*)(uint64_t)loader_stack[3] + (uint64_t)&kernmem - (uint64_t)&physbase),
    (uint64_t*)&physbase,
    (uint64_t*)(uint64_t)loader_stack[4]
  );
 /* for(
    temp1 = "!!!!! start() returned !!!!!", temp2 = (char*)0xb8000;
    *temp1;
    temp1 += 1, temp2 += 2
  ) *temp2 = *temp1;*/
  while(1) __asm__ volatile ("hlt");
}
