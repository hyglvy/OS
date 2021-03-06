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
#include <sys/utils.h>
#include <sys/kstring.h>

uint64_t prep_stack(uint64_t *tos2, char* argv[], char *envp[], char *filename) {
	int argc = 1, len; //filename comes first	
        uint8_t *tos = (uint8_t *)tos2;

	if (argv != NULL) {
		while (argv[argc-1] != NULL) {
			if (argv[argc-1][0] != '\0') {
				//kprintf("inside prep stack %s\n",argv[argc-1]);
				len = kstrlen(argv[argc-1]);
				tos -= len;
				kmemcpy((char *)tos,argv[argc-1],len);
				//char *eos = (char *)(tos) + len - 1; 
				*(tos+len-1) = '\0';
				//kprintf("index %d value %s ptr %p\n",(argc-1),(char *)tos,tos);
				argv[argc-1] = (char *)tos;
				//kprintf("argv opied ptr %p content %s\n",argv[argc-1],argv[argc-1]);
				argc++;
			}
			else if (argv[argc-1][0] == '\0') {
				break;
			}
		}		
	}
	//kprintf("1\n");
	len = kstrlen(filename);
	//kprintf("2\n");
	tos -= len;
	//kprintf("3\n");
	kmemcpy((char *)tos,filename,len);
	//kprintf("4\n");
	*(tos+len-1) = '\0';
	//kprintf("5\n");
	char *fptr = (char *)tos;	
	//kprintf("fname ptr %p content %s\n",fptr,fptr);
	int envp_count = 0;
	if (envp != NULL) {
                while (envp[envp_count] != NULL) {
			if (envp[envp_count][0] != '\0') {
				len = kstrlen(envp[envp_count]);
                        	tos -= len;
                        	kmemcpy((char *)tos,envp[envp_count],len);
				*(tos+len-1) = '\0';
                        	envp[envp_count] = (char *)tos;
				//kprintf("envp copied ptr %p content %s\n",argv[argc-1],argv[argc-1]);
                        	envp_count++;
			}
			else if (envp[envp_count][0] == '\0') {
				break;
                        }
                }
        }
	
	uint64_t *stacktos = (uint64_t *)(((uint64_t)tos >> 6) << 6);
	//tos2 = (uint64_t *)tos;
	stacktos--; 
	*stacktos = '\0'; stacktos--;
	for (int x=(envp_count-1); x >= 0 ; x--) {
		*stacktos = (uint64_t)envp[x];
		stacktos--;
	}
	*stacktos = '\0'; stacktos--;
	for (int x= (argc-2); x >= 0; x--) {
		*stacktos = (uint64_t)argv[x];
		stacktos--;
	}
	*stacktos = (uint64_t)fptr;
	stacktos--;
	*stacktos = argc;
	//kprintf("%x %x %x %x\n",*stacktos,*(stacktos+1),*(stacktos+2),*(stacktos+3));
	return (uint64_t)stacktos;
}

Task *loadElf(char *fileName, char *argv[], char *envp[]) { 
	struct posix_header_ustar * header = (struct posix_header_ustar *)&_binary_tarfs_start;
	//kprintf("size of header %d",sizeof(struct posix_header_ustar));
	while(header<(struct posix_header_ustar *)&_binary_tarfs_end) {
		uint64_t size = octalToDecimal(stoi(header->size));
    		if (size == 0) {
			header++;
		}
		else {
			//kprintf("\nFileName: %s\n", header->name);
			Elf64_Ehdr *elfhdr = (Elf64_Ehdr *) (header+1);
			if((elfhdr->e_ident[0]==0x7f)&&(elfhdr->e_ident[1]==0x45)&&
			(elfhdr->e_ident[2]==0x4c)&&(elfhdr->e_ident[3]==0x46)&& (!kstrcmp(header->name,fileName))) {
				Task *new_task = (Task*) kmalloc(sizeof(Task));
				kstrcpy(new_task->cwd,"/rootfs/bin/");
				/*char tokens[10][100];
				char *token = kstrtok(header->name, '/');
				int token_index = 0;
				while(token!= NULL) {
					kstrcpy(tokens[token_index],token);
					kfree((uint64_t*)token);
					token = kstrtok(NULL, '/');
					token_index++;
				}
				for(int i = 0; i < token_index-1; i++) {
					kstrcat(new_task->cwd, tokens[i]);
					kstrcat(new_task->cwd, "/");
				}
				kprintf("\n CWD %s\n", new_task->cwd);*/
				new_task->pid = (last_assn_pid+1)%MAX_PROC;
				last_assn_pid = new_task->pid;
				new_task->mm = (struct mm_struct *) kmalloc((sizeof(struct mm_struct)));
				new_task->mm->vm_begin = NULL;
				uint8_t *data = (uint8_t *)(header+1);
				Elf64_Phdr *proghdr = (Elf64_Phdr *)&data[elfhdr->e_phoff];
				struct vma* iter;
				int i;
				uint64_t end_addr = 0;
				uint64_t newcr3 = create_table(); //preps the PMl4 table only
				uint64_t oldcr3;
				__asm__ __volatile__("movq %%cr3, %0\n\t"
						    :"=a"(oldcr3));
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
						vm->vma_mem_size = proghdr[i].p_memsz;
						vm->vma_flags = proghdr[i].p_flags;
						vm->vma_next = NULL;
						vm->vm_type = NORMAL;
						if(new_task->mm->vm_begin == NULL) {
							new_task->mm->vm_begin = vm;
						}
						else {
							for(iter = new_task->mm->vm_begin; iter->vma_next != NULL; iter = iter->vma_next);
							iter->vma_next = vm;
						}
						__asm__ __volatile__("movq %0, %%cr3\n\t"
						    ::"a"(newcr3));
						uint64_t start_assign_page_vaddr = (proghdr[i].p_vaddr >> 12) << 12;
						uint64_t end_assign_page_vaddr = ((proghdr[i].p_vaddr + proghdr[i].p_memsz + 4095) >> 12) << 12;
						int no_pages = ((end_assign_page_vaddr - start_assign_page_vaddr) / 4096);
						for(int i =0; i < no_pages; i++) {
							put_page_mapping(USER_ACCESSIBLE, start_assign_page_vaddr, newcr3);
							start_assign_page_vaddr += 4096;
						}
						//no_pages = 1;
            					kmemcpy((char*) proghdr[i].p_vaddr, (char*) &data[proghdr[i].p_offset], proghdr[i].p_filesz);
						memset((uint8_t*)proghdr[i].p_vaddr + proghdr[i].p_filesz, 0, proghdr[i].p_memsz - proghdr[i].p_filesz);
						__asm__ __volatile__("movq %0, %%cr3\n\t"
						    ::"a"(oldcr3));
					}
				}
				end_addr += 4096;
				end_addr = (end_addr >> 12) << 12;
				// Go to the end of the Vma list and add vma for heap
				for(iter = new_task->mm->vm_begin; iter->vma_next != NULL; iter = iter->vma_next);
				struct vma *vm = (struct vma*) kmalloc(sizeof(struct vma));
				vm->vma_start = (uint64_t *) end_addr;
				vm->vma_end = (uint64_t *) end_addr;
				new_task->mm->brk_begin = end_addr;
				vm->vm_type = HEAP;
				vm->vma_next = NULL;
				iter->vma_next = vm;
				// Add vma entry for stack
				struct vma *vm_stack = (struct vma*) kmalloc(sizeof(struct vma));
				vm_stack->vma_start = (uint64_t *) (USER_STACK - USER_STACK_SIZE);
				//kprintf("stack vm start in loadelf %x USER stack %x user stack size %x\n",(uint64_t)vm_stack->vma_start,USER_STACK, USER_STACK_SIZE);
				vm_stack->vma_end = (uint64_t *) USER_STACK;
				vm_stack->vma_next = NULL;
				vm_stack->vm_type = STACK;
				vm->vma_next = vm_stack;
				// Allocate 1 page for stack for now and add it to the new cr3 page mapping
				
				// creating duplicate filename
				int strl = kstrlen(fileName); // for xyz\0 it returns 4
				char *newfileName = (char *)kmalloc(strl);
				kstrcpy(newfileName, fileName);
				newfileName[strl-1] = '\0';
				// creating duplicate argv
				char **duplargv=NULL;
				int argc = 0;
				#if 0
				if (argv != NULL) {kprintf("load elf argv was not null 2 %p\n",argv);
				kprintf("%s\n",argv[0]);}
				#endif
				if (argv != NULL) {
                			while (argv[argc] != NULL) argc++;
					duplargv = (char **)kmalloc((argc+1)*sizeof(char *));
					int x=0;
					while (argv[x] != NULL) {
						//kprintf("x = %d\n",x);
						int len = kstrlen(argv[x]); // returns 4 for xyz\0
						duplargv[x] = (char *)kmalloc(len); //4
						kstrcpy(duplargv[x],argv[x]);
						duplargv[x][len-1] = '\0';
						x++;
					}
					duplargv[argc] = NULL;
        			}
				//if (argv != NULL)
				//	kprintf("print before prep stack %s\n",duplargv[0]);
				// creating duplicate envp
				char **duplenvp = NULL;
				int envcount = 0;
                                if (envp != NULL) {
                                        while (envp[envcount] != NULL) envcount++;
					duplenvp = (char **)kmalloc((envcount+1)*sizeof(char *));
                                        int x=0;
                                        while (envp[x] != NULL) {
                                                int len = kstrlen(envp[x]); // returns 4 for xyz\0
                                                duplenvp[x] = kmalloc(len); //4
                                                kstrcpy(duplenvp[x],envp[x]);
                                                duplenvp[x][len-1] = '\0';
                                                x++;
                                        }
                                        duplenvp[envcount] = NULL;
                                }
				__asm__ __volatile__("movq %0, %%cr3\n\t"
						    ::"a"(newcr3));
				new_task->mm->pg_pml4=newcr3;
				new_task->next = NULL;
				new_task->prev = NULL;
				new_task->children = NULL;
				new_task->sibling = NULL;
				new_task->regs.cr3 = newcr3;
				put_page_mapping(USER_ACCESSIBLE,USER_STACK, newcr3);
				put_page_mapping(USER_ACCESSIBLE,USER_STACK - 4096, newcr3);
				new_task->mm->stack_begin = (uint64_t) (USER_STACK);
				// prep stack
				uint64_t tos = prep_stack((uint64_t *)(new_task->mm->stack_begin), duplargv, duplenvp, newfileName);
				new_task->mm->stack_begin = tos;
				// Allocating a dummy file obj for stdin so its not null
				new_task->file_desc[0] = (struct FILE_OBJ*)kmalloc(sizeof(struct FILE_OBJ));
				/*
				new_task->file_desc[0]->file_begin = USER_READ_BUFFER;
				new_task->file_desc[0]->file_end = USER_READ_BUFFER+4095;
				new_task->file_desc[0]->file_offset = 0;
				*/
				int x;
				for(x=1; x< MAX_FDS;x++) {
					new_task->file_desc[x] = NULL;
					new_task->dir_desc[x] = NULL;
				}
                                // may move above part
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


