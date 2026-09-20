.syntax unified
.cpu cortex-m4
.fpu fpv4-sp-d16
.thumb

.global PendSV_Handler
.global task_yield
.extern current_sp
.extern schedule_next_task

@ EXC_RETURN bit 4 (0x10): 1 = integer frame, 0 = extended frame with s16-s31.
@ Each task stores its own EXC_RETURN in the software stack frame (r14).

.thumb_func
.type PendSV_Handler, %function
PendSV_Handler:
    cpsid i

    mrs r0, psp
    cbz r0, skip_save

    tst lr, #0x10
    it eq
    vstmdbeq r0!, {s16-s31}

    stmdb r0!, {r4-r11, r14}

    ldr r1, =current_sp
    str r0, [r1]

skip_save:
    bl schedule_next_task

    ldr r0, =current_sp
    ldr r0, [r0]

    ldmia r0!, {r4-r11, r14}

    tst lr, #0x10
    it eq
    vldmiaeq r0!, {s16-s31}

    msr psp, r0
    isb

    cpsie i
    bx lr

.thumb_func
.type task_yield, %function
task_yield:
    ldr r0, =0xE000ED04       @ ICSR
    ldr r1, =0x10000000       @ PENDSVSET
    str r1, [r0]
    dsb
    isb
    bx lr