/*
 * arch/arm/mach-kinetis/suspend.S
 * Kinetis low-level suspend-to-RAM code
 *
 * Copyright (C) 2012 Robert Brehm, <brehm@mci.sdu.dk>
 * Copyright (C) 2014-2017 Emcraft Systems
 * Vladimir Khusainov, Emcraft Systems, <vlad@emcraft.com>
 *
 * See file CREDITS for list of people who contributed to this
 * project.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

/*
 * This code runs from eSRAM (on-chip SRAM).
 */

	.global kinetis_suspend_to_ram
	.global kinetis_suspend_to_ram_end
	.align 4

kinetis_suspend_to_ram:
	push {lr}

	/*
	 * Put DDR into self-refresh
	 */
	ldr r0, ddr_cr16
	mov r1, #0x10000
	add r1, #0x0
	str r1, [r0]

	ldr r0, ddr_cr15
	mov r1, #0x10000
	add r1, #0xF
	str r1, [r0]

	ldr r0, sim_mcr
	ldr r1, [r0]
	add r1, #0x1
	str r1, [r0]

	/*
	 * Disable all DDR I/O pins
	 * Disable DDR_DQS
	 */
	ldr r0, sim_mcr
	ldr r1, [r0]
	movw r2, #0xFFF3
	movt r2, #0xFFFF
	and r1,r2
	str r1, [r0]

	/*
	 * Disable clock to the DDR controller
	 */
	ldr r0, sim_scgc3
	ldr r1, [r0]
	movw r2, #0x0
	movt r2, #0xBFFF
	and r1,r2
	str r1, [r0]

	/*
	 * We switch the MCG mode from PEE all the way to BLPI.
	 * BLPI = PLL is disabled, FLL is disabled, clock is derived
	 * from the internal reference clock.
	 * We need that so that on wake up from VLPS the Kinetis
	 * returns to BLPI, rather than automatically return to PBE,
	 * resetting the clocks and hence baud rates for UART, etc.
	 *
	 * Switch from PEE to PBE
	 */
	ldr r0, mcg_c1
	ldrb r1, [r0]
	orr r1, #0x80
	strb r1, [r0]

	/*
	 * Wait until CLKS has switched to PBE
	 */
1:
	ldr r0, mcg_s
	ldrb r1, [r0]
	and r1, #0x0C
	cmp r1, #0x08
	bne 1b

	/*
	 * Switch from PBE to FBE
	 */
	ldr r0, mcg_c6
	ldrb r1, [r0]
	and r1, #0xBF
	strb r1, [r0]

	/*
	 * Wait until CLKS has switched to FBE
	 */
1:
	ldr r0, mcg_s
	ldrb r1, [r0]
	and r1, #0x20
	cmp r1, #0x00
	bne 1b

	/*
	 * Switch from FBE to FBI
	 */
	ldr r0, mcg_c1
	ldrb r1, [r0]
	and r1, 0x3F
	//orr r1, #0x46
	orr r1, #0x40
	strb r1, [r0]

	/*
	 * Wait until CLKS has switched to FBI
	 */
1:
	ldr r0, mcg_s
	ldrb r1, [r0]
	and r1, #0x0C
	cmp r1, #0x04
	bne 1b

	/*
	 * Switch from FBI to BLPI. Set LP
	 */
	ldr r0, mcg_c2
	ldrb r1, [r0]
	orr r1, #0x02
	strb r1, [r0]

	/*
	 * Switch to Deep Sleep
	 */
	dsb
	isb

	nop
	nop
	nop
	nop

	wfi

	/*
	 * When at this point, we have been woken from Deep Sleep.
	 * Since we entered VLPS in BLPI Mode, we exit in BLPI mode as well.
	 */

	/*
	 * Switch from BLPI to FBI. Unset LP
	 */
	ldr r0, mcg_c2
	ldrb r1, [r0]
	and r1, #0xFD
	strb r1, [r0]

	/*
	 * Switch from FBI to FBE
	 */
	ldr r0, mcg_c1
	ldrb r1, [r0]
	and r1, #0x3B
	orr r1, #0x82
	strb r1, [r0]

	/*
	 * Wait until reference clock has switched to external clock
	 */
1:
	ldr r0, mcg_s
	ldrb r1, [r0]
	and r1, #0x10
	cmp r1, #0x10
	beq 1b

	/*
	 * Wait until CLKS has switched to FBE
	 */
1:
	ldr r0, mcg_s
	ldrb r1, [r0]
	and r1, #0x0C
	cmp r1, #0x08
	bne 1b

	/*
	 * Switch from FBE to PBE
	 */
	ldr r0, mcg_c6
	ldrb r1, [r0]
	orr r1, #0x40
	strb r1, [r0]

	/*
	 * Wait until PLL1 has locked up
	 */
1:
	ldr r0, mcg_s2
	ldrb r1, [r0]
	and r1, #0x40
	cmp r1, #0x40
	bne 1b

	/*
	 * Switch from PBE to PEE
	 */
	ldr r0, mcg_c1
	ldrb r1, [r0]
	and r1, #0x3F
	strb r1, [r0]

	/*
	 * Wait until CLKS switches to PEE
	 */
1:
	ldr r0, mcg_s
	ldrb r1, [r0]
	and r1, #0x0C
	cmp r1, #0x0C
	bne 1b

	/*
	 * Enable all DDR I/O pins
	 * Enable DDR_DQS
	 */
	ldr r0, sim_mcr
	ldr r1, [r0]
	add r1, #0xC
	str r1, [r0]

	/*
	 * Enable the clock to the DDR controller
	 */
	ldr r0, sim_scgc3
	ldr r1, [r0]
	orr r1, #0x4000
	str r1, [r0]

	/*
	 * Take DDR out of self-refresh
	 */
	ldr r0, sim_mcr
	ldr r1, [r0]
	movw r2, #0xFFFE
	movt r2, #0xFFFF
	and r1,r2
	str r1, [r0]

	ldr r0, ddr_cr15
	mov r1, #0xF
	str r1, [r0]

	nop
	nop
	nop
	nop

	/*
	 * Return to the kernel code running from DDR
	 */
	pop {pc}
	bx lr

	.align 4
ddr_cr15	:
	.word 0x400AE03C
ddr_cr16	:
	.word 0x400AE040
sim_mcr:
	.word 0x4004806C
sim_scgc3:
	.word 0x40048030
mcg_c1:
	.word 0x40064000
mcg_c2:
	.word 0x40064001
mcg_c6:
	.word 0x40064005
mcg_s:
	.word 0x40064006
mcg_s2:
	.word 0x40064012

	.align 4
kinetis_suspend_to_ram_end:
	.word 0
