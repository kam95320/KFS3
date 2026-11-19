#include <stdint.h>
#include "paging.h"
#include "pmm.h"
#include "vmm.h"

#define CR0_WP (1u << 16)
#define PF_PRESENT 1u
#define PF_WRITE 2u
#define PF_USER 4u
#define PTE_FLAGS (PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER)

extern int pmm_test_exhaustion(void);
extern void vmm_test_fault_entry(void);
extern void vmm_test_user_done(void);
extern uint32_t vmm_test_read(uint32_t virt);
extern void vmm_test_write(uint32_t virt);
extern void vmm_test_enter_user(uint32_t virt);
extern char vmm_test_read_ip[], vmm_test_write_ip[], vmm_test_user_ip[];
extern char vmm_test_read_resume[], vmm_test_write_resume[];

struct table_register {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

struct idt_gate {
    uint16_t low;
    uint16_t selector;
    uint16_t attributes;
    uint16_t high;
} __attribute__((packed));

static struct idt_gate idt[0x31];
static uint64_t test_gdt[8];
static uint32_t tss[26];
static uint32_t fault_stack[1024] __attribute__((aligned(16)));
static struct table_register original_gdt;

static uintptr_t expected_ip;
static uintptr_t resume_ip;
static volatile unsigned fault_count;
static volatile uint32_t fault_address;
static volatile uint32_t fault_error;

static void port_write(uint16_t port, uint8_t value)
{
    __asm__ volatile ("outb %0, %1" :: "a"(value), "Nd"(port));
}

static void report(const char *message)
{
    while (*message)
        port_write(0xe9, (uint8_t)*message++);
}

static void finish(uint8_t status) __attribute__((noreturn));

static void finish(uint8_t status)
{
    port_write(0xf4, status);
    for (;;)
        __asm__ volatile ("cli; hlt");
}

void vmm_test_boot_failure(void)
{
    report("BOOT FAIL\n");
    finish(0x11);
}

static void set_gate(unsigned vector, void (*entry)(void), uint16_t attributes)
{
    uintptr_t address = (uintptr_t)entry;

    idt[vector] = (struct idt_gate){address & 0xffffu, 0x08u,
                                   attributes, address >> 16};
}

static void install_fault_handler(void)
{
    const struct table_register idtr = {sizeof(idt) - 1u, (uintptr_t)idt};

    set_gate(14, vmm_test_fault_entry, 0x8e00u);
    set_gate(0x30, vmm_test_user_done, 0xee00u);
    __asm__ volatile ("lidt %0" :: "m"(idtr) : "memory");
}

uintptr_t vmm_test_page_fault(uintptr_t ip, uint32_t error, uint32_t address)
{
    if (expected_ip == 0 || ip != expected_ip || fault_count != 0)
    {
        report("UNEXPECTED PAGE FAULT\n");
        finish(0x11);
    }
    fault_address = address;
    fault_error = error;
    ++fault_count;
    return resume_ip;
}

static void arm_fault(const char *instruction, const char *resume)
{
    fault_count = 0;
    expected_ip = (uintptr_t)instruction;
    resume_ip = (uintptr_t)resume;
}

static int check_fault(uint32_t address, uint32_t error)
{
    expected_ip = 0;
    return fault_count == 1 && fault_address == address && fault_error == error;
}

static int test_alignment(void)
{
    uint32_t phys = frame_alloc();
    uint32_t translated;

    if (!phys || vmm_map_page(VMM_START + 1u, phys, PAGE_WRITABLE) == 0 ||
        vmm_map_page(VMM_START, phys + 1u, PAGE_WRITABLE) == 0 ||
        vmm_get_physical(VMM_START, &translated) == 0)
        return -1;
    if (vmm_map_page(VMM_START, phys, PAGE_WRITABLE) != 0 ||
        vmm_unmap_page(VMM_START + 1u) == 0 ||
        vmm_get_physical(VMM_START, &translated) != 0 || translated != phys ||
        vmm_unmap_page(VMM_START) != 0)
        return -1;
    return frame_free(phys);
}

static int test_limits(void)
{
    const uint32_t invalid[] = {VMM_START - PAGE_SIZE, VMM_END, 0xfffff000u};
    uint32_t phys = frame_alloc();
    uint32_t translated;
    unsigned i;

    if (!phys)
        return -1;
    for (i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i)
    {
        if (vmm_map_page(invalid[i], phys, PAGE_WRITABLE) == 0 ||
            vmm_unmap_page(invalid[i]) == 0 ||
            vmm_get_physical(invalid[i], &translated) == 0)
            return -1;
    }
    if (vmm_map_page(VMM_END - PAGE_SIZE, phys, 0) != 0 ||
        vmm_unmap_page(VMM_END - PAGE_SIZE) != 0)
        return -1;
    return frame_free(phys);
}

static int test_translation(void)
{
    uint32_t virt = VMM_START + PAGE_SIZE;
    uint32_t phys = frame_alloc();
    uint32_t translated;
    int valid;

    if (!phys || vmm_map_page(virt, phys, PAGE_WRITABLE) != 0)
        return -1;
    valid = vmm_get_physical(virt + 123u, &translated) == 0 &&
            translated == phys + 123u &&
            vmm_get_physical(virt + PAGE_SIZE - 1u, &translated) == 0 &&
            translated == phys + PAGE_SIZE - 1u;
    if (vmm_unmap_page(virt) != 0 || frame_free(phys) != 0)
        return -1;
    return valid ? 0 : -1;
}

static int test_reuse(void)
{
    uint32_t phys = frame_alloc();
    uint32_t reused;
    uint32_t virt = VMM_START;

    if (!phys || vmm_map_page(virt, phys, PAGE_WRITABLE) != 0 ||
        vmm_unmap_page(virt) != 0 || frame_free(phys) != 0)
        return -1;

    reused = frame_alloc();
    if (!reused || vmm_map_page(virt, reused, PAGE_WRITABLE) != 0 ||
        vmm_unmap_page(virt) != 0 || frame_free(reused) != 0)
        return -1;
    return reused == phys ? 0 : -1;
}

static int test_multiple(void)
{
    uint32_t frames[3];
    uint32_t translated;
    unsigned i;

    for (i = 0; i < 3; ++i)
    {
        uint32_t virt = VMM_START + i * PAGE_SIZE;

        frames[i] = frame_alloc();
        if (!frames[i] || vmm_map_page(virt, frames[i], PAGE_WRITABLE) != 0)
            return -1;
        *(volatile uint32_t *)(uintptr_t)virt = 0xabc00000u + i;
    }
    for (i = 0; i < 3; ++i)
    {
        uint32_t virt = VMM_START + i * PAGE_SIZE;

        if (vmm_get_physical(virt, &translated) != 0 || translated != frames[i] ||
            *(volatile uint32_t *)(uintptr_t)virt != 0xabc00000u + i)
            return -1;
        if (vmm_unmap_page(virt) != 0 || frame_free(frames[i]) != 0)
            return -1;
    }
    return 0;
}

static uint32_t *active_directory(void)
{
    /* The page directory and bootstrap table are in identity-mapped kernel BSS. */
    return (uint32_t *)(uintptr_t)paging_directory_phys();
}

static void enable_user_probe(void)
{
    uint32_t base = (uintptr_t)tss;
    struct table_register gdtr = {sizeof(test_gdt) - 1u, (uintptr_t)test_gdt};
    const volatile uint64_t *current;
    unsigned i;

    __asm__ volatile ("sgdt %0" : "=m"(original_gdt));
    current = (const volatile uint64_t *)(uintptr_t)original_gdt.base;
    for (i = 0; i < 7; ++i)
        test_gdt[i] = current[i];

    tss[1] = (uintptr_t)(fault_stack + 1024);
    tss[2] = 0x18u;
    tss[25] = (uint32_t)sizeof(tss) << 16;
    test_gdt[7] = (sizeof(tss) - 1u) | ((uint64_t)(base & 0xffffffu) << 16) |
                  ((uint64_t)0x89u << 40) | ((uint64_t)(base >> 24) << 56);
    __asm__ volatile ("lgdt %0; ltr %%ax" :: "m"(gdtr), "a"(0x38u) : "memory");
}

static int test_user_restriction(uint32_t virt)
{
    uint32_t *directory = active_directory();
    uint32_t *identity = (uint32_t *)(uintptr_t)(directory[0] & 0xfffff000u);
    uintptr_t code = (uintptr_t)vmm_test_user_ip;
    uint32_t *entry;
    uint32_t saved;
    uint32_t saved_identity = directory[0];
    uint32_t saved_vmm = directory[1];
    int valid;

    if (code >= VMM_START || code % PAGE_SIZE != 0)
        return 0;
    entry = &identity[code / PAGE_SIZE];
    saved = *entry;
    enable_user_probe();
    directory[0] |= PAGE_USER;
    *entry = (saved | PAGE_USER) & ~PAGE_WRITABLE;
    /* Permit CPL 3 at the PDE so the target PTE alone denies the read. */
    directory[1] |= PAGE_USER;
    __asm__ volatile ("invlpg (%0); invlpg (%1)" :: "r"(code), "r"(virt) : "memory");
    /* Positive control: CPL 3 can read the dedicated user page. */
    arm_fault(vmm_test_user_ip, 0);
    vmm_test_enter_user(code);
    valid = fault_count == 0;
    arm_fault(vmm_test_user_ip, 0);
    vmm_test_enter_user(virt);
    if (!check_fault(virt, PF_PRESENT | PF_USER))
        valid = 0;

    *entry = saved;
    directory[0] = saved_identity;
    directory[1] = saved_vmm;
    __asm__ volatile ("invlpg (%0); invlpg (%1); lgdt %2"
                      :: "r"(code), "r"(virt), "m"(original_gdt) : "memory");
    return valid;
}

static int test_permissions(void)
{
    const uint32_t virt = VMM_START;
    volatile uint32_t *ptr = (volatile uint32_t *)(uintptr_t)virt;
    uint32_t phys = frame_alloc();
    uint32_t cr0;
    int valid;

    if (!phys || vmm_map_page(virt, phys, PAGE_USER) == 0 ||
        vmm_map_page(virt, phys, PAGE_WRITABLE) != 0)
        return -1;
    valid = (paging_vmm_table()[0] & PTE_FLAGS) == (PAGE_PRESENT | PAGE_WRITABLE);
    *ptr = 0x12345678u;
    if (vmm_unmap_page(virt) != 0 || vmm_map_page(virt, phys, 0) != 0)
        return -1;
    valid = valid && (paging_vmm_table()[0] & PTE_FLAGS) == PAGE_PRESENT &&
            (active_directory()[1] & PTE_FLAGS) == (PAGE_PRESENT | PAGE_WRITABLE) &&
            *ptr == 0x12345678u;

    __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
    __asm__ volatile ("mov %0, %%cr0" :: "r"(cr0 | CR0_WP) : "memory");
    arm_fault(vmm_test_write_ip, vmm_test_write_resume);
    vmm_test_write(virt);
    if (!check_fault(virt, PF_PRESENT | PF_WRITE) || *ptr != 0x12345678u)
        valid = 0;
    __asm__ volatile ("mov %0, %%cr0" :: "r"(cr0) : "memory");

    if (!test_user_restriction(virt))
        valid = 0;
    if (vmm_unmap_page(virt) != 0 || frame_free(phys) != 0)
        return -1;
    return valid ? 0 : -1;
}

static int test_unmapped_fault(void)
{
    const uint32_t virt = VMM_START;
    const uint32_t address = virt + 128u;
    uint32_t phys = frame_alloc();
    uint32_t translated;

    if (!phys || vmm_map_page(virt, phys, PAGE_WRITABLE) != 0)
        return -1;
    *(volatile uint32_t *)(uintptr_t)address = 0x87654321u;
    if (vmm_test_read(address) != 0x87654321u || vmm_unmap_page(virt) != 0 ||
        vmm_get_physical(address, &translated) == 0)
        return -1;

    arm_fault(vmm_test_read_ip, vmm_test_read_resume);
    vmm_test_read(address);
    if (!check_fault(address, 0))
        return -1;
    return frame_free(phys);
}

static int table_empty(void)
{
    unsigned i;

    for (i = 0; i < PAGE_ENTRIES; ++i)
    {
        if (paging_vmm_table()[i] != 0)
            return 0;
    }
    return 1;
}

void vmm_test_run(void)
{
    static const struct {
        const char *id;
        int (*run)(void);
    } tests[] = {
        {"T01", vmm_test_basic}, {"T02", vmm_test_duplicate},
        {"T03", test_alignment}, {"T04", test_limits},
        {"T05", test_translation}, {"T06", test_reuse},
        {"T07", test_multiple}, {"T08", pmm_test_exhaustion},
        {"T09", test_permissions}, {"T10", test_unmapped_fault}
    };
    unsigned i;
    uint32_t initial = pmm_free_count();

    install_fault_handler();
    report("VMM BEGIN\n");
    for (i = 0; i < sizeof(tests) / sizeof(tests[0]); ++i)
    {
        report(tests[i].id);
        report(" START\n");
        if (tests[i].run() != 0 || pmm_free_count() != initial || !table_empty())
        {
            report(tests[i].id);
            report(" FAIL\n");
            finish(0x11);
        }
        report(tests[i].id);
        report(" PASS\n");
    }
    report("VMM END\n");
    finish(0x10);
}
