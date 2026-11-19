

#include <stdint.h>
#include "pmm.h"

/* Conserver les adresses physiques des frames allouees. */
static uint32_t frames[PMM_FRAME_COUNT];

int pmm_test_exhaustion(void)
{
    uint32_t initial;
    uint32_t count;
    uint32_t phys;
    uint32_t i;

    initial = pmm_free_count();
    count = 0;

    if (initial == 0)
        return -1;

    /* 1. Allouer toutes les frames disponibles. */
    while ((phys = frame_alloc()) != 0)
    {
        if (count >= initial ||
            count >= PMM_FRAME_COUNT ||
            phys % PMM_PAGE_SIZE != 0)
            return -1;

        frames[count++] = phys;
    }

    /* 2. Verifier que la memoire est epuisee. */
    if (count != initial || pmm_free_count() != 0)
        return -1;

    /* 3. Liberer toutes les frames allouees. */
    for (i = 0; i < count; ++i)
    {
        if (frame_free(frames[i]) != 0)
            return -1;
    }

    /* 4. Verifier la restauration du compteur. */
    if (pmm_free_count() != initial)
        return -1;

    return 0;
}
