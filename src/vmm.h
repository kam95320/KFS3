
#ifndef VMM_H
#define VMM_H

#include <stdint.h>

int vmm_map_page(uint32_t virt,
                 uint32_t phys,
                 uint32_t flags);

int vmm_unmap_page(uint32_t virt);

int vmm_get_physical(uint32_t virt,
                     uint32_t *phys);
int vmm_test_basic(void);
int vmm_test_duplicate(void);
#endif
