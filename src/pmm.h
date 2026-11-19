
#ifndef PMM_H
#define PMM_H

#include <stdint.h>

#define PMM_PAGE_SIZE    4096u
#define PMM_PHYS_LIMIT   (10u * 1024u * 1024u)
#define PMM_FRAME_COUNT  (PMM_PHYS_LIMIT / PMM_PAGE_SIZE)

int pmm_init(uint32_t magic, uint32_t mbi_phys);

uint32_t frame_alloc(void);
int frame_free(uint32_t phys_addr);

uint32_t pmm_free_count(void);

#endif

