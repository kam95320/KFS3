
#ifndef PAGING_H
#define PAGING_H

#include <stdint.h>

#define PAGE_SIZE       4096u
#define PAGE_ENTRIES    1024u

#define PAGE_PRESENT    0x001u
#define PAGE_WRITABLE   0x002u
#define PAGE_USER       0x004u

#define VMM_START       0x00400000u
#define VMM_END         0x00800000u

int paging_prepare(void);

uint32_t paging_directory_phys(void);
uint32_t *paging_vmm_table(void);

#endif
