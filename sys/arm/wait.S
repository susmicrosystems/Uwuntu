.global arch_waitq_sleep
.type arch_waitq_sleep, %function
arch_waitq_sleep:
	mrc p15, #0, r0, c13, c0, #4
	ldr r0, [r0, #4]
	add r0, #280
	stm r0, {r4-r13}
	str lr, [r0, #0x2C]
	mrs r1, cpsr
	str r1, [r0, #0x30]
	b waitq_sleep_return
