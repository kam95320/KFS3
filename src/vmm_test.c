
#include "vmm.h"
#include "pmm.h"
#include "paging.h"

#include <stdint.h>

int vmm_test_basic(void)
{
    const uint32_t virt = VMM_START;
    const uint32_t value = 0x12345678u;

    uint32_t initial;
    uint32_t phys;
    uint32_t translated;
    volatile uint32_t *ptr;
    int result = 0;

    initial = pmm_free_count();

    /* 1. Allouer une frame physique. */
    phys = frame_alloc();

    if (phys == 0u)
        return -1;

    /* 2. Creer le mapping. */
    if (vmm_map_page(
            virt, phys, PAGE_WRITABLE) != 0)
    {
        frame_free(phys);
        return -2;
    }

    /* La frame reste allouee apres le demapping. */
    ptr = (volatile uint32_t *)(uintptr_t)virt;
    if (vmm_get_physical(virt, &translated) != 0 || translated != phys)
        result = -3;
    else
    {
        *ptr = value;
        if (*ptr != value)
            result = -4;
    }

    if (vmm_unmap_page(virt) != 0 ||
        vmm_get_physical(virt, &translated) == 0)
        return -5;

    if (pmm_free_count() != initial - 1u)
        result = -6;
    if (frame_free(phys) != 0 || pmm_free_count() != initial)
        return -7;

    return result;
}


int vmm_test_duplicate(void)
{
    const uint32_t virt = VMM_START;
    uint32_t before;
    uint32_t first;
    uint32_t second;
    uint32_t translated;
    int result = -1;
    int mapped = 0;

    before = pmm_free_count();

    first = frame_alloc();
    if (first == 0)
        return -1;

    second = frame_alloc();
    if (second == 0)
    {
        frame_free(first);
        return -1;
    }

    /* Premier mapping : doit reussir. */
    if (vmm_map_page(virt, first, PAGE_WRITABLE) != 0)
        goto cleanup;

    mapped = 1;

    /* Deuxieme mapping : doit etre refuse. */
    if (vmm_map_page(virt, second, PAGE_WRITABLE) == 0)
        goto cleanup;

    /* Verifier que le premier mapping existe encore. */
    if (vmm_get_physical(virt, &translated) != 0)
        goto cleanup;

    if (translated != first)
        goto cleanup;

    result = 0;

cleanup:
    if (mapped && (vmm_unmap_page(virt) != 0 ||
                   vmm_get_physical(virt, &translated) == 0))
        return -2;

    if (frame_free(second) != 0)
        result = -1;

    if (frame_free(first) != 0)
        result = -1;

    if (pmm_free_count() != before)
        result = -1;

    return result;
}
