
#include "vmm.h"
#include "paging.h"

#include <stdint.h>

/*
 * Verifie qu'une adresse appartient a la
 * zone reservee au VMM et est alignee.
 */
static int valid_virtual_page(uint32_t virt)
{
    if (virt < VMM_START || virt >= VMM_END)
        return 0;

    if (virt % PAGE_SIZE != 0u)
        return 0;

    return 1;
}

/*
 * Cree un mapping virtuel vers une frame
 * physique deja connue.
 *
 * Ne prend pas possession de la frame.
 */
int vmm_map_page(uint32_t virt,
                 uint32_t phys,
                 uint32_t flags)
{
    uint32_t *table;
    uint32_t index;

    if (!valid_virtual_page(virt))
        return -1;

    if (phys % PAGE_SIZE != 0u)
        return -1;

    if (flags & ~(PAGE_WRITABLE))
        return -1;

    table = paging_vmm_table();

    index = (virt >> 12) & 0x3FFu;

    /* Ne jamais ecraser un mapping existant. */
    if (table[index] != 0u)
        return -1;

    table[index] =
        phys |
        flags |
        PAGE_PRESENT;

    /* Invalider la traduction mise en cache. */
    __asm__ volatile (
        "invlpg (%0)"
        :
        : "r"((uintptr_t)virt)
        : "memory"
    );

    return 0;
}

/*
 * Supprime un mapping sans liberer
 * la frame physique correspondante.
 */
int vmm_unmap_page(uint32_t virt)
{
    uint32_t *table;
    uint32_t index;

    if (!valid_virtual_page(virt))
        return -1;

    table = paging_vmm_table();

    index = (virt >> 12) & 0x3FFu;

    if (!(table[index] & PAGE_PRESENT))
        return -1;

    table[index] = 0u;

    __asm__ volatile (
        "invlpg (%0)"
        :
        : "r"((uintptr_t)virt)
        : "memory"
    );

    return 0;
}

/*
 * Retrouve l'adresse physique correspondant
 * a une adresse virtuelle.
 *
 * Accepte aussi un offset dans la page.
 */
int vmm_get_physical(uint32_t virt,
                     uint32_t *phys)
{
    uint32_t *table;
    uint32_t index;
    uint32_t offset;
    uint32_t entry;

    if (phys == 0)
        return -1;

    if (virt < VMM_START || virt >= VMM_END)
        return -1;

    table = paging_vmm_table();

    index = (virt >> 12) & 0x3FFu;
    offset = virt & 0xFFFu;

    entry = table[index];

    if (!(entry & PAGE_PRESENT))
        return -1;

    *phys = (entry & 0xFFFFF000u) | offset;

    return 0;
}
