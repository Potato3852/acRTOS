.syntax unified
.cpu cortex-m4
.thumb

.global PendSV_Handler
.global task_yield
.extern current_sp
.extern schedule_next_task

.type PendSV_Handler, %function
PendSV_Handler:
    mrs r0, psp
    cbz r0, skip_save

    stmdb r0!, {r4-r11}
    ldr r1, =current_sp
    str r0, [r1]

skip_save:
    push {lr}
    bl schedule_next_task
    pop {lr}

    ldr r0, =current_sp
    ldr r0, [r0]

    ldmia r0!, {r4-r11}
    msr psp, r0

    orr lr, lr, #0x04
    bx lr

.type task_yield, %function
task_yield:
    ldr r0, =0xE000ED04       @ ICSR
    ldr r1, =0x10000000       @ PENDSVSET
    str r1, [r0]
    bx lr