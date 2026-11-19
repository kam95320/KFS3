
#include "pmm.h"

#include <stdint.h>
#include <stddef.h>

#define MULTIBOOT_MAGIC   0x2BADB002u
#define MULTIBOOT_MMAP    (1u << 6)
#define IDENTITY_LIMIT    0x00400000u
#define BITMAP_SIZE       (PMM_FRAME_COUNT / 8u)

extern char __kernel_end[];

static uint8_t used_bitmap[BITMAP_SIZE];
static uint8_t allocated_bitmap[BITMAP_SIZE];

static uint32_t free_frames;
static int initialized;

static int bit_get(const uint8_t *bitmap, uint32_t index)
{
    return (bitmap[index / 8u] >> (index % 8u)) & 1u;
}

static void bit_set(uint8_t *bitmap, uint32_t index)
{
    bitmap[index / 8u] |= (uint8_t)(1u << (index % 8u));
}

static void bit_clear(uint8_t *bitmap, uint32_t index)
{
    bitmap[index / 8u] &=
        (uint8_t)~(1u << (index % 8u));
}

static uint32_t read32(const uint8_t *p)
{
    return (uint32_t)p[0]
        | ((uint32_t)p[1] << 8)
        | ((uint32_t)p[2] << 16)
        | ((uint32_t)p[3] << 24);
}

/*
 * Rend disponibles uniquement les frames entierement
 * contenues dans une region RAM utilisable.
 */
static void mark_usable(uint32_t start, uint32_t end)
{
    uint32_t first;
    uint32_t last;
    uint32_t i;

    if (start >= end)
        return;

    if (start < 0x00100000u)
        start = 0x00100000u;

    if (end > PMM_PHYS_LIMIT)
        end = PMM_PHYS_LIMIT;

    if (start >= end)
        return;

    first = start / PMM_PAGE_SIZE;
    if (start % PMM_PAGE_SIZE)
        ++first;

    last = end / PMM_PAGE_SIZE;

    for (i = first; i < last; ++i)
    {
        if (bit_get(used_bitmap, i))
        {
            bit_clear(used_bitmap, i);
            ++free_frames;
        }
    }
}

/* Reserve toute frame qui chevauche cette region. */
static void mark_reserved(uint32_t start, uint32_t end)
{
    uint32_t first;
    uint32_t last;
    uint32_t i;

    if (start >= end || start >= PMM_PHYS_LIMIT)
        return;

    if (end > PMM_PHYS_LIMIT)
        end = PMM_PHYS_LIMIT;

    first = start / PMM_PAGE_SIZE;
    last = end / PMM_PAGE_SIZE;

    if (end % PMM_PAGE_SIZE)
        ++last;

    for (i = first; i < last; ++i)
    {
        if (!bit_get(used_bitmap, i))
        {
            bit_set(used_bitmap, i);
            --free_frames;
        }
    }
}

int pmm_init(uint32_t magic, uint32_t mbi_phys)
{
    const uint8_t *mbi;
    const uint8_t *mmap;
    uint32_t flags;
    uint32_t mmap_length;
    uint32_t mmap_addr;
    uint32_t offset;
    uint32_t kernel_end;
    uint32_t i;
    uint32_t pass;

    initialized = 0;
    free_frames = 0;

    if (magic != MULTIBOOT_MAGIC)
        return -1;

    /* La structure doit etre accessible sans paging. */
    if (mbi_phys > IDENTITY_LIMIT - 52u)
        return -1;

    mbi = (const uint8_t *)(uintptr_t)mbi_phys;
    flags = read32(mbi);

    if (!(flags & MULTIBOOT_MMAP))
        return -1;

    mmap_length = read32(mbi + 44u);
    mmap_addr = read32(mbi + 48u);

    if (!mmap_length || mmap_addr >= IDENTITY_LIMIT)
        return -1;

    if (mmap_length > IDENTITY_LIMIT - mmap_addr)
        return -1;

    kernel_end = (uint32_t)(uintptr_t)__kernel_end;

    if (kernel_end >= PMM_PHYS_LIMIT)
        return -1;

    for (i = 0; i < BITMAP_SIZE; ++i)
    {
        used_bitmap[i] = 0xFFu;
        allocated_bitmap[i] = 0u;
    }

    mmap = (const uint8_t *)(uintptr_t)mmap_addr;

    /*
     * Deux passages :
     * 1. Autoriser la RAM utilisable.
     * 2. Re-reserver les regions non utilisables.
     */
    for (pass = 0; pass < 2u; ++pass)
    {
        offset = 0;

        while (offset < mmap_length)
        {
            const uint8_t *entry = mmap + offset;
            uint32_t size;
            uint32_t base_lo;
            uint32_t base_hi;
            uint32_t len_lo;
            uint32_t len_hi;
            uint32_t type;
            uint32_t end;

            if (mmap_length - offset < 4u)
                return -1;

            size = read32(entry);

            if (size < 20u ||
                size > mmap_length - offset - 4u)
                return -1;

            base_lo = read32(entry + 4u);
            base_hi = read32(entry + 8u);
            len_lo = read32(entry + 12u);
            len_hi = read32(entry + 16u);
            type = read32(entry + 20u);

            /*
             * Nous ne gerons que les 10 premiers Mio.
             * Les regions au-dessus de 4 Gio sont ignorees.
             */
            if (base_hi == 0u)
            {
                if (len_hi != 0u ||
                    len_lo > UINT32_MAX - base_lo)
                    end = UINT32_MAX;
                else
                    end = base_lo + len_lo;

                if (pass == 0u && type == 1u)
                    mark_usable(base_lo, end);

                if (pass == 1u && type != 1u)
                    mark_reserved(base_lo, end);
            }

            offset += size + 4u;
        }
    }

    /* Exclure la memoire basse et l'image noyau. */
    mark_reserved(0u, 0x00100000u);
    mark_reserved(0x00100000u, kernel_end);

    initialized = 1;
    return 0;
}

uint32_t frame_alloc(void)
{
    uint32_t i;

    if (!initialized)
        return 0;

    for (i = 0; i < PMM_FRAME_COUNT; ++i)
    {
        if (!bit_get(used_bitmap, i))
        {
            bit_set(used_bitmap, i);
            bit_set(allocated_bitmap, i);
            --free_frames;

            return i * PMM_PAGE_SIZE;
        }
    }

    return 0;
}

int frame_free(uint32_t phys_addr)
{
    uint32_t index;

    if (!initialized ||
        phys_addr % PMM_PAGE_SIZE ||
        phys_addr >= PMM_PHYS_LIMIT)
        return -1;

    index = phys_addr / PMM_PAGE_SIZE;

    if (!bit_get(allocated_bitmap, index))
        return -1;

    bit_clear(allocated_bitmap, index);
    bit_clear(used_bitmap, index);
    ++free_frames;

    return 0;
}

uint32_t pmm_free_count(void)
{
    return initialized ? free_frames : 0u;
}

