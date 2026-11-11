.syntax unified
.cpu cortex-m0plus
.thumb

.section .text.sk6812_send_frame, "ax", %progbits
.global sk6812_send_frame
.type sk6812_send_frame, %function
.thumb_func
/* r0: GPIO BSRR, r1: set mask, r2: reset mask, r3: early reset masks. */
sk6812_send_frame:
    push {r4, lr}
    /* At 24 MHz, aim for 0.3 us / 0.75 us high and ~1.4 us per bit.
       Writing zero to BSRR leaves the pin high until the later reset. */
    .rept 24
        ldmia r3!, {r4}
        str r1, [r0]
        .rept 6
            nop
        .endr
        str r4, [r0]
        .rept 10
            nop
        .endr
        str r2, [r0]
        .rept 12
            nop
        .endr
    .endr
    pop {r4, pc}
.size sk6812_send_frame, .-sk6812_send_frame
