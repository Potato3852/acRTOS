.syntax unified
.cpu cortex-m4
.thumb

.global PendSV_Handler
.global task_yield
.type PendSV_Handler, %function
.thumb_func

.extern current_sp
.extern schedule_next_task

task_yield:
    ldr r0, =0xE000ED04   // SCB->ICSR
    ldr r1, =0x10000000   // SCB_ICSR_PENDSVSET_Msk
    str r1, [r0]
    bx lr

PendSV_Handler:
    mrs r0, psp
    cbz r0, start_first_task

    stmdb r0!, {r4-r11}
    ldr r1, =current_sp
    str r0, [r1]

    push {lr}
    bl schedule_next_task
    pop {lr}

start_first_task:
    ldr r1, =current_sp
    ldr r0, [r1]

    ldmia r0!, {r4-r11}
    msr psp, r0

    mov r0, #2
    msr control, r0
    isb

    ldr lr, =0xFFFFFFFD
    bx lr