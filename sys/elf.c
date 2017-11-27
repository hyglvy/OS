#include <sys/elf64.h>
#include <sys/tarfs.h>
#include <sys/defs.h>
#include <sys/kprintf.h>
#include <sys/kernel_threads.h>
#include <sys/kmalloc.h>
#include <sys/paging.h>
#include <sys/kmemcpy.h>
#include <sys/string.h>
#include <sys/copy_tables.h>
#include <sys/scheduler.h>
// TODO: change this function
uint64_t power (uint64_t x, int e) {
    if (e == 0) return 1;
    return x * power(x, e-1);
}
uint64_t octalToDecimal(uint64_t octal)
{
    uint64_t decimal = 0, i=0;
    while(octal!=0){
        decimal = decimal + (octal % 10) * power(8,i++);
        octal = octal/10;
    }
    return decimal;
}
// TODO: change this function
uint64_t stoi(char *s) // the message and then the line #
{
    uint64_t i;
    i = 0;
    while(*s >= '0' && *s <= '9')
    {
        i = i * 10 + (*s - '0');
        s++;
    }
    return i;
}
// GSAHA: added to test page fault handler
/*void switch_user_mode(uint64_t symbol) {
        __asm__ __volatile__ ( "cli\n\t"
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
                        ::"b"(symbol)
        );
}*/
Task *loadElf(char *fileName) { 
	struct posix_header_ustar * header = (struct posix_header_ustar *)&_binary_tarfs_start;
	kprintf("size of header %d",sizeof(struct posix_header_ustar));
	while(header<(struct posix_header_ustar *)&_binary_tarfs_end) {
		uint64_t size = octalToDecimal(stoi(header->size));
    		if (size == 0) {
			header++;
		}
		else {
			Elf64_Ehdr *elfhdr = (Elf64_Ehdr *) (header+1);
			if((elfhdr->e_ident[0]==0x7f)&&(elfhdr->e_ident[1]==0x45)&&
			(elfhdr->e_ident[2]==0x4c)&&(elfhdr->e_ident[3]==0x46)&& (!strcmp(header->name,fileName))) {
				Task *new_task = (Task*) kmalloc(sizeof(Task));
				// TODO: insert into run queue in scheduler
				/*
				if (run_queue == NULL) {
					run_queue = new_task;
				}
				else {
					new_task->next = run_queue;
					run_queue = new_task;
				}
				*/
				// TODO: need to set CURRENT_TASK and pid in the scheduler
				// CURRENT_TASK = new_task;	
				//new_task->pid = (last_assn_pid+1)%MAX_PROC;
				//last_assn_pid = new_task->pid;
				new_task->mm = (struct mm_struct *) kmalloc((sizeof(struct mm_struct)));
				new_task->mm->vm_begin = NULL;
				uint8_t *data = (uint8_t *)(header+1);
				Elf64_Phdr *proghdr = (Elf64_Phdr *)&data[elfhdr->e_phoff];
				struct vma* iter;
				int i;
				uint64_t end_addr = 0;
				for(i = 0; i < elfhdr->e_phnum; i++) {
					if(proghdr[i].p_type == ELF_PT_LOAD) {
						struct vma *vm = (struct vma*) kmalloc(sizeof(struct vma));
						vm->vma_start = (uint64_t *)proghdr[i].p_vaddr;
						if((proghdr[i].p_vaddr + proghdr[i].p_memsz) > end_addr) {
							end_addr = (proghdr[i].p_vaddr + proghdr[i].p_memsz);
						}
						vm->vma_end = (uint64_t *)(proghdr[i].p_vaddr + proghdr[i].p_memsz);
						vm->vma_file_ptr = (uint64_t *)(&data[proghdr[i].p_offset]); 
						// GSAHA: added to test page fault handle
						vm->vma_file_offset = 0;

						vm->vma_size = proghdr[i].p_filesz;
						vm->vma_flags = proghdr[i].p_flags;
						vm->vma_next = NULL;
						if(new_task->mm->vm_begin == NULL) {
							new_task->mm->vm_begin = vm;
						}
						else {
							for(iter = new_task->mm->vm_begin; iter->vma_next != NULL; iter = iter->vma_next);
							iter->vma_next = vm;
						} 
					}
				}
				end_addr += 4096;
				end_addr = (end_addr >> 12) << 12;
				// Go to the end of the Vma list and add vma for heap
				for(iter = new_task->mm->vm_begin; iter->vma_next != NULL; iter = iter->vma_next);
				struct vma *vm = (struct vma*) kmalloc(sizeof(struct vma));
				vm->vma_start = (uint64_t *) end_addr;
				vm->vma_end = (uint64_t *) end_addr;
				vm->vma_next = NULL;
				iter->vma_next = vm;
				// Add vma entry for stack
				struct vma *vm_stack = (struct vma*) kmalloc(sizeof(struct vma));
				vm_stack->vma_start = (uint64_t *) 0xc0000000 - 0x10000000;
				vm_stack->vma_end = (uint64_t *) 0xc0000000;
				vm_stack->vma_next = NULL;
				vm->vma_next = vm_stack;
				// Allocate 1 page for stack for now and add it to the new cr3 page mapping
				uint64_t newcr3 = create_table();
				uint64_t oldcr3;
				__asm__ __volatile__("movq %%cr3, %0\n\t"
						    :"=a"(oldcr3));
				__asm__ __volatile__("movq %0, %%cr3\n\t"
						    ::"a"(newcr3));
				new_task->mm->pg_pml4=newcr3;
				new_task->next = NULL;
				new_task->regs.cr3 = newcr3;
				put_page_mapping(USER_ACCESSIBLE,0xc0000000, newcr3);
				put_page_mapping(USER_ACCESSIBLE,0xc0000000 - 4096, newcr3);
				new_task->mm->stack_begin = (uint64_t) (0xc0000000);
				__asm__ __volatile__("movq %0, %%cr3\n\t"
						    ::"a"(oldcr3));        

				// might need to change the part above
				new_task->mm->e_entry = elfhdr->e_entry;
				return new_task;
			}
			size = (size%512==0) ? size +512: size + 512 + (512-size%512);
			header = (struct posix_header_ustar *) (((uint64_t)(header)) + size);
		}
	}
	return NULL; 
}


