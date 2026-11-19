#include <stdint.h>
#include "gdt.h"
#include "stack.h"
#include "paging.h"
#include "pmm.h"
#include "vmm.h"
extern int pmm_test_exhaustion(void);
#ifdef VMM_TEST
extern void vmm_test_run(void) __attribute__((noreturn));
extern void vmm_test_boot_failure(void);
#endif

#define MULTIBOOT_BOOTLOADER_MAGIC 0x2BADB002u

extern void paging_enable(uint32_t directory_phys);

static void kernel_halt(void)
{
#ifdef VMM_TEST
    vmm_test_boot_failure();
#endif
    for (;;)
        __asm__ volatile ("cli; hlt" ::: "memory");
}

void kernel_main(unsigned int magic, unsigned int boot_info)
{
    uint32_t first;
    uint32_t second;
    uint32_t before;

    if (magic != MULTIBOOT_BOOTLOADER_MAGIC)
        kernel_halt();

    /* Initialiser la gestion physique avant la pagination. */
    if (pmm_init(magic, boot_info) != 0)
        kernel_halt();

    /* Test d'allocation de deux frames. */
    before = pmm_free_count();

    first = frame_alloc();
    second = frame_alloc();

    if (!first || !second || first == second)
        kernel_halt();

    if (pmm_free_count() != before - 2u)
        kernel_halt();

    /* Tester la liberation et la reutilisation. */
    if (frame_free(first) != 0)
        kernel_halt();

    if (frame_alloc() != first)
        kernel_halt();

    /* Tester la protection contre une double liberation. */
    if (frame_free(first) != 0)
        kernel_halt();

    if (frame_free(first) == 0)
        kernel_halt();

    if (frame_free(second) != 0)
        kernel_halt();

    if (pmm_free_count() != before)
        kernel_halt();

    /* AJOUT 1 : refuser de liberer une frame reservee au noyau. */
    if (frame_free(0x00108000u) == 0)
        kernel_halt();

    /* AJOUT 2 : refuser une adresse non alignee sur 4 Kio. */
    if (frame_free(0x0010B001u) == 0)
            kernel_halt();

    /* AJOUT 3 : verifier que le compteur reste inchange. */
    if (pmm_free_count() != before)
            kernel_halt();

    /* CODE EXISTANT : conserver ici. */
    if (paging_prepare() != 0)
            kernel_halt();
#ifndef VMM_TEST
    /* Tester l'epuisement et la restitution des frames. */
    if (pmm_test_exhaustion() != 0)
        kernel_halt();

#endif
    paging_enable(paging_directory_phys());
    gdt_test();
    stack_dump();
#ifdef VMM_TEST
    vmm_test_run();
#else
    if (vmm_test_basic() != 0)
        kernel_halt();
    if (vmm_test_duplicate() != 0)
        kernel_halt();
    kernel_halt();
#endif
}
