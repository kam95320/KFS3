BITS 32

section .text
extern vmm_test_page_fault
global vmm_test_fault_entry, vmm_test_user_done
global vmm_test_read, vmm_test_write, vmm_test_enter_user
global vmm_test_read_ip, vmm_test_write_ip, vmm_test_user_ip
global vmm_test_read_resume, vmm_test_write_resume

vmm_test_fault_entry:
    pushad
    mov eax, cr2
    push eax
    push dword [esp + 36]  ; CPU error code
    push dword [esp + 44]  ; CPU saved EIP
    call vmm_test_page_fault
    add esp, 12
    test byte [esp + 40], 3 ; CPU saved CS
    jnz vmm_test_user_done
    mov [esp + 36], eax
    popad
    add esp, 4
    iretd

vmm_test_read:
    mov eax, [esp + 4]
vmm_test_read_ip:
    mov eax, [eax]
vmm_test_read_resume:
    ret

vmm_test_write:
    mov eax, [esp + 4]
vmm_test_write_ip:
    mov dword [eax], 0
vmm_test_write_resume:
    ret

vmm_test_enter_user:
    mov eax, [esp + 4]
    mov [kernel_esp], esp
    ; The user probe uses registers only and never touches its stack.
    push dword 0x33
    push dword 0x00400000
    push dword 2           ; IF stays clear
    push dword 0x23
    push dword vmm_test_user_ip
    mov dx, 0x2b
    mov ds, dx
    mov es, dx
    iretd

vmm_test_user_done:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov esp, [kernel_esp]
    ret

; Only this page of the identity map is made accessible at CPL 3.
align 4096
vmm_test_user_ip:
    mov eax, [eax]
    int 0x30
align 4096

section .bss
align 4
kernel_esp: resd 1

section .note.GNU-stack noalloc noexec nowrite progbits
