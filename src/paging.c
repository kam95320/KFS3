
#include "paging.h"

#include <stdint.h>

/* Tables statiques, alignees sur 4 Kio. */
static uint32_t page_directory[PAGE_ENTRIES]
    __attribute__((aligned(PAGE_SIZE)));

static uint32_t first_page_table[PAGE_ENTRIES]
    __attribute__((aligned(PAGE_SIZE)));

static uint32_t second_page_table[PAGE_ENTRIES]
    __attribute__((aligned(PAGE_SIZE)));

int paging_prepare(void)
{
    uint32_t i;

    /*
     * Fonction a appeler uniquement avant
     * l'activation de la pagination.
     */
    for (i = 0; i < PAGE_ENTRIES; ++i)
    {
        page_directory[i] = 0;
        first_page_table[i] = 0;
        second_page_table[i] = 0;
    }

    /* Identity mapping des 4 premiers Mio. */
    for (i = 0; i < PAGE_ENTRIES; ++i)
    {
        first_page_table[i] =
            (i * PAGE_SIZE) |
            PAGE_PRESENT |
            PAGE_WRITABLE;
    }

    /* PDE[0] : identity mapping. */
    page_directory[0] =
        (uint32_t)(uintptr_t)first_page_table |
        PAGE_PRESENT |
        PAGE_WRITABLE;

    /*
     * PDE[1] : deuxieme table presente,
     * mais aucune page virtuelle mappee.
     */
    page_directory[1] =
        (uint32_t)(uintptr_t)second_page_table |
        PAGE_PRESENT |
        PAGE_WRITABLE;

    if (((uintptr_t)page_directory & 0xFFFu) != 0)
        return -1;

    if (((uintptr_t)first_page_table & 0xFFFu) != 0)
        return -1;

    if (((uintptr_t)second_page_table & 0xFFFu) != 0)
        return -1;

    if (page_directory[0] !=
        ((uint32_t)(uintptr_t)first_page_table | 3u))
        return -1;

    if (page_directory[1] !=
        ((uint32_t)(uintptr_t)second_page_table | 3u))
        return -1;

    if (first_page_table[0] != 0x00000003u)
        return -1;

    if (first_page_table[184] != 0x000B8003u)
        return -1;

    if (first_page_table[256] != 0x00100003u)
        return -1;

    if (first_page_table[1023] != 0x003FF003u)
        return -1;

    if (second_page_table[0] != 0u)
        return -1;

    return 0;
}

uint32_t paging_directory_phys(void)
{
    return (uint32_t)(uintptr_t)page_directory;
}

uint32_t *paging_vmm_table(void)
{
    return second_page_table;
}
