;cputype.asm
    TITLE   cputype
    DOSSEG
    .model  small

;CPUID   MACRO
;    db  0fh     ; Hardcoded CPUID instruction
;    db  0a2h
;ENDM

.data
old_INT6_offset dw  0
old_INT6_segment dw 0

.code
.586
    public  _cpu_type
_cpu_type   proc
    mov ax,00FFh        ; clear CPU type return register
    push    sp          ; save SP on stack to look at
    pop bx          ; get SP saved on stack
    cmp bx,sp           ; if 8086/8088, these values will differ
    jnz short @_8086    ; 86 or 186

    ; > 8186
    enter   6,0         ; create stack frame
    push    di
    mov     ax,offset INT6_handler
    push    cs
    push    ax
    call    set_INT6_vector     ; set pointer to our INT6 handler
    add     sp,4

    mov     di,offset @_80286   ; address to catch invalid instructin

    mov     edx,edx     ; only 386+ can execute

    mov     di,offset @_80386
    xadd    dx,dx   ; not supported on 368

    mov     di,offset @no_cpuid

    mov     eax,0
    CPUID
    cmp     eax,1
    jl      @no_cpuid

    mov     eax,1
    CPUID
    mov     al,ah
    and     al,0fh
    cmp     al,4
    jle     @486
    mov     ah,100
    mul     ah
    add     ax,86
    ; dx    contains feature bits
    push    ax
    push    dx
    call    restore_INT6_vector
    pop     dx
    pop     ax
    pop     di
    leave
    ret

@486:
@no_cpuid:
    call    restore_INT6_vector
    xor     dx,dx
    mov     ax,486
    pop     di
    leave
    ret

@_80386:
    call    restore_INT6_vector
    xor     dx,dx
    mov     ax,386
    pop     di
    leave
    ret

@_80286:
    call    restore_INT6_vector
    xor     dx,dx
    mov     ax,286
    pop     di
    leave
    ret
@_8086:
    xor     dx,dx
    mov     ax,86
    ret

INT6_handler:
    enter   0,0         ; create new stack frame
    mov     word ptr ss:[bp][2],di  ; change return address
    leave
    iret
_cpu_type endp

set_INT6_vector proc
    ; save old int6 vector
    enter   0,0
    mov     ax,3506h
    int     21h
    mov     old_int6_offset,bx
    mov     old_int6_segment,es

    push    ds
    lds     dx,[bp+4]
    mov     ax,2506h
    int     21h
    pop     ds
    leave
    ret
set_INT6_vector endp

restore_INT6_vector proc
    
    push    ds

    lds     dx,dword ptr old_int6_offset
    ;mov     ds,old_int6_segment
    mov     ax,2506h
    int     21h
    pop     ds

    ret
restore_INT6_vector endp


end
