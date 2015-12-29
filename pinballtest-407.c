#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include "stm32f407.auto.h"
#include "eval_pins.h"
#include "fader.h"
#include "gpio.h"
#include "lcdscreen.h"
#include "regtim.h"
#include "advtim.h"
#include "usart.h"

#define RGB16(r,g,b) ((r & 0x1f) | ((g & 0x3f) << 5) | ((b & 0x1f) << 11)) /* I think */
#define RGB8(r,g,b) ((r & 7) | ((g & 3) << 3) | ((b & 7) << 5))
/*
 * Timers with Encoder support:
 *	TIM 1/8
 		TIM1
			ch1: PA8  PE9
			ch2: PA9  PE11
		TIM8:
			ch1: PC6  PI5
			ch2: PC7  PI6
	TIM 2/3/4/5
		TIM2
			ch1  PA0? PA5? PA15
			ch2  PA1 PB3
		TIM3
			ch1  PA6 PB4 PC6
			ch2  PA7 PB5 PC7
		TIM4
			ch1  PB6 PD12
			ch2  PB7 PD13
		TIM5
			ch1  PA0 PH10
			ch2  PA1 PH11



	PI not on board
	PE9/11 used by LCD
	PC6/7 are UART6 RX/TX but can be used, if the jumpers are removed.
	PA8/9 are GPIO1/vbus (even better? no. vbus detect moves too slow)
	PA0/1 are pushbutton/RMII (RMII line is clock or something; couldn't get rid of it after a half hour)
	PA6/7 are SPI or DCMI/ETH. Might be able to use)
	PB4/5 are PSMout (TIM3) on baseboard.
	PB6/7 are I2C_SCL (audio) and DCMI_VSYNC/UART. Could use
	PD12/13 are LED and FSMC. Can't use
	PH10/11 doesn't exist (on the eval board)

Solution: Use PB4/5 (TIM3)

 *
 *  Encoder configs:
 	TIMx_SMCR[SMS] = 1 to count TI2 edges
	TIMx_SMCR[SMS] = 2 to count TI1 edges
	TIMx_SMCR[SMS] = 3 to count both edges
	
	TIMx_CCER[CC1P] = polarity of TI1
	TIMx_CCER[CC2P] = polarity of TI2
	TIMx_CCER[CC1NP] = 0
	TIMx_CCER[CC2NP] = 0
	TIMx_ARR = range of counter
 */

/* Project pinout
 *
 * pin  dir symbol      attributes
 *
 * //PA0  IN  PUSHBUTTON
 * PA0  ?   ?           TIM12
 * //PA1  ?   ?           TIM12
 * PA10 IN  USBID       PUPD
 *
 * PB3  ?   ?           TIM12
 * PB4  ?   ?           TIM3
 * PB5  ?   ?           TIM3
 * PB6  OUT             GPIO
 *
 * PC0  OUT USBPOWER en
 * PC6  ?   ?           TIM891011
 * PC7  ?   ?           TIM891011
 * PC8  ?   ?           TIM891011
 * PC9  ?   ?           TIM891011
 * PC10 ?   ?           UART4
 * PC11 ?   ?           UART4
 *
 * PD0  ?               FSMC
 * PD1  ?   DISP_D3     FSMC
 * PD2  ?   ?
 * PD3  OUT DISP_RESETn GPIO
 * PD4  ?   DISP_RD     FSMC
 * //PD5  IN  USBPOWERFAULT
 * PD5  ?   DISP_WRWE   FSMC
 * PD6  ?   ?
 * PD7  ?   DISP_CS?    FSMC
 * PD8  ?   DISP_D13    FSMC
 * PD9  ?   DISP_D14    FSMC
 * PD10 ?   DISP_D15    FSMC
 * PD11 OUT CLICKFEEL   GPIO
 * PD12 OUT GREEN       TIM345
 * PD13 OUT ORANGE      TIM345
 * PD14 OUT RED         FSMC/SDIO/USB_OTG
 * PD15 OUT BLUE        FSMC/SDIO/USB_OTG
 *
 * //PE2  ?   ?           GPIO
 * PE3  ?   DISP_DC (data/command)
 * PE7  ?   DISP_D4     FSMC
 * PE8  ?   DISP_D5     FSMC
 * PE9  ?   DISP_D6     FSMC
 * PE10 ?   DISP_D7     FSMC
 * PE11 ?   DISP_D8     FSMC
 * PE12 ?   DISP_D9     FSMC
 * PE13 ?   DISP_D10    FSMC
 * PE14 ?   DISP_D11    FSMC
 * PE15 ?   DISP_D12    FSMC
 */

void unhandled()
{
	while(true) {;}
}

int a = 5;

int skipPll = 0;

int usbPowerWanted = 0;




uint8_t screen[160][120];

/** Set the drawable window.
 * 0 <= x < 320
 * 0 <- y < 240
 */
void lcdSetWindow(int x1, int y1, int x2, int y2)
{
	int startx = x1 < x2 ? x1 : x2;
	int endx = x1 > x2 ? x1 : x2;
	int starty = y1 < y2 ? y1 : y2;
	int endy = y1 > y2 ? y1 : y2;

	if (endx >= 0x320 || endy >= 0x240) {
		return;
	}
	lcdWriteReg(0x44, starty | (endy << 8));
	lcdWriteReg(0x45, startx);
	lcdWriteReg(0x46, endx);
	lcdWriteReg(0x4E, startx); /* GRAM X address */
	lcdWriteReg(0x4F, starty); /* GRAM Y address */
}

void lcdFillBox(int x1, int y1, int x2, int y2, uint16_t color)
{
	int x, y;
	lcdSetWindow(x1, y1, x2, y2);
	setLCDMode(LCD_MODE_COMMAND);
	writeLCD_raw(0x22);
	setLCDMode(LCD_MODE_DATA);
	for (y = y1; y < y2; y++) {
		for (x = x1; x < x2 + 1; x++) {
			writeLCD_raw(color);
		}
	}

}

void drawBackground(void)
{
	int x, y;
	lcdSetWindow(0,0,319,239);
	setLCDMode(LCD_MODE_COMMAND);
	writeLCD_raw(0x22);
	setLCDMode(LCD_MODE_DATA);
	for (y = 0; y < 240; y++) {
		for (x = 0; x < 320; x++) {
			writeLCD_raw(RGB16(x*0x14/320, y*0x2f/240, 0));
		}
	}
}

int main()
{
	uint16_t time = 0;
	uint16_t cutoff = 0x4;
	uint16_t meh = 0;
	int dir = 0;
	bool setIt = false;
	int buttonState = 0;

	RCC.ahb1En |= RCC_AHB1EN_GPIOAEN_MASK;
	RCC.ahb1En |= RCC_AHB1EN_GPIOBEN_MASK;
	RCC.ahb1En |= RCC_AHB1EN_GPIOCEN_MASK;
	RCC.ahb1En |= RCC_AHB1EN_GPIODEN_MASK;
	RCC.ahb1En |= RCC_AHB1EN_GPIOEEN_MASK;
	RCC.apb1En |= RCC_APB1EN_TIM5EN_MASK;
	RCC.apb1En |= RCC_APB1EN_TIM4EN_MASK;
	RCC.apb1En |= RCC_APB1EN_TIM3EN_MASK;
	RCC.apb1En |= RCC_APB1EN_TIM2EN_MASK;
	RCC.apb2En |= RCC_APB2EN_TIM8EN_MASK;
	RCC.ahb3En |= RCC_AHB3EN_FSMCEN_MASK;

	GPA.mode |= (GPIO_MODE_MODE_INPUT << (PUSHBUTTON * 2));
	GPA.mode |= (GPIO_MODE_MODE_INPUT << (USBID * 2));
	GPA.pupd |= (GPIO_PUPD_PUPD_PULL_UP << (USBID * 2));
	GPC.od |= (1 << USBPOWER);
	GPC.mode |= (GPIO_MODE_MODE_GENERAL_OUTPUT << (USBPOWER * 2));
	GPD.mode |= (GPIO_MODE_MODE_ALTERNATE << (GREEN * 2));
	GPD.mode |= (GPIO_MODE_MODE_ALTERNATE << (ORANGE * 2));
	GPD.od |= (1 << ORANGE);
	GPD.mode |= (GPIO_MODE_MODE_ALTERNATE << (RED * 2));
	GPD.mode |= (GPIO_MODE_MODE_ALTERNATE << (BLUE * 2));
	GPD.mode |= (GPIO_MODE_MODE_ALTERNATE << (0 * 2));


	GPD.mode |= (GPIO_MODE_MODE_GENERAL_OUTPUT << (3 * 2)); // RESET
	GPD.od |= (1 << 3);
	GPD.mode |= (GPIO_MODE_MODE_ALTERNATE << (1 * 2)); // D3
	GPD.mode |= (GPIO_MODE_MODE_ALTERNATE << (5 * 2)); // WR/WE
	GPD.mode |= (GPIO_MODE_MODE_ALTERNATE << (4 * 2)); // RD
	GPD.mode |= (GPIO_MODE_MODE_ALTERNATE << (7 * 2)); // CS?
	GPE.mode |= (GPIO_MODE_MODE_ALTERNATE << (7 * 2)); // D4
	GPE.mode |= (GPIO_MODE_MODE_ALTERNATE << (8 * 2)); // D5
	GPE.mode |= (GPIO_MODE_MODE_ALTERNATE << (9 * 2)); // D6
	GPE.mode |= (GPIO_MODE_MODE_ALTERNATE << (10 * 2)); // D7
	GPE.mode |= (GPIO_MODE_MODE_ALTERNATE << (11 * 2)); // D8
	GPE.mode |= (GPIO_MODE_MODE_ALTERNATE << (12 * 2)); // D9
	GPE.mode |= (GPIO_MODE_MODE_ALTERNATE << (13 * 2)); // D10
	GPE.mode |= (GPIO_MODE_MODE_ALTERNATE << (14 * 2)); // D11
	GPE.mode |= (GPIO_MODE_MODE_ALTERNATE << (15 * 2)); // D12
	GPD.mode |= (GPIO_MODE_MODE_ALTERNATE << (8 * 2)); // D13
	GPD.mode |= (GPIO_MODE_MODE_ALTERNATE << (9 * 2)); // D14
	GPD.mode |= (GPIO_MODE_MODE_ALTERNATE << (10 * 2)); // D15
	GPD.mode |= (GPIO_MODE_MODE_GENERAL_OUTPUT << (11 * 2)); // PD11/GPIO4, for wiring to clickFeel relay.
	GPD.od |= (1 << 11);
	GPE.mode |= (GPIO_MODE_MODE_GENERAL_OUTPUT << (3 * 2)); // DC (Data/cmd)
	GPE.od |= (1 << 3); /* Command (register) mode */
	gpio_setAltFunc(&GPD, ORANGE, GPIO_AF_TIM_3_4_5);
	gpio_setAltFunc(&GPD, RED, GPIO_AF_FSMC_SDIO_USB_OTG);
	gpio_setAltFunc(&GPD, GREEN, GPIO_AF_TIM_3_4_5);
	gpio_setAltFunc(&GPD, BLUE, GPIO_AF_FSMC_SDIO_USB_OTG);
	gpio_setAltFunc(&GPD, 0, GPIO_AF_FSMC_SDIO_USB_OTG);
	gpio_setAltFunc(&GPD, 1, GPIO_AF_FSMC_SDIO_USB_OTG);
	gpio_setAltFunc(&GPD, 4, GPIO_AF_FSMC_SDIO_USB_OTG);
	gpio_setAltFunc(&GPD, 5, GPIO_AF_FSMC_SDIO_USB_OTG);
	gpio_setAltFunc(&GPD, 7, GPIO_AF_FSMC_SDIO_USB_OTG);
	gpio_setAltFunc(&GPE, 7, GPIO_AF_FSMC_SDIO_USB_OTG);
	gpio_setAltFunc(&GPE, 8, GPIO_AF_FSMC_SDIO_USB_OTG);
	gpio_setAltFunc(&GPE, 9, GPIO_AF_FSMC_SDIO_USB_OTG);
	gpio_setAltFunc(&GPE, 10, GPIO_AF_FSMC_SDIO_USB_OTG);
	gpio_setAltFunc(&GPE, 11, GPIO_AF_FSMC_SDIO_USB_OTG);
	gpio_setAltFunc(&GPE, 12, GPIO_AF_FSMC_SDIO_USB_OTG);
	gpio_setAltFunc(&GPE, 13, GPIO_AF_FSMC_SDIO_USB_OTG);
	gpio_setAltFunc(&GPE, 14, GPIO_AF_FSMC_SDIO_USB_OTG);
	gpio_setAltFunc(&GPE, 15, GPIO_AF_FSMC_SDIO_USB_OTG);
	gpio_setAltFunc(&GPD, 8, GPIO_AF_FSMC_SDIO_USB_OTG);
	gpio_setAltFunc(&GPD, 9, GPIO_AF_FSMC_SDIO_USB_OTG);
	gpio_setAltFunc(&GPD, 10, GPIO_AF_FSMC_SDIO_USB_OTG);

#if 0
	TIM4.cr1 |= TIMX_CR1_CEN_MASK;
	TIM4.ccr1 = 0x200;
	TIM4.ccr2 = 0x0060; /* Brightness of ORANGE, AKA LCD Backlight. Range: 0 -- TIM4.arr */
	TIM4.ccr3 = 0xa000;
	TIM4.ccr4 = 0x50;
	TIM4.arr = 0x00ff; /* Maximum counter. Limits compare for PWM. Larger values give more precision to duty cycle. Smaller values give faster cycle times. (setting too high can result in a noticable buzzing sound from the LCD backlight controller) */
	//TIM4.ccmr1 |= TIMX_CCMR1_OC2M_PWM1 | TIMX_CCMR1_OC2PE_MASK;
	TIM4.ccmr1 = 0x6868;
	TIM4.ccmr2 = 0x6868;
	TIM4.cr1 |= TIMX_CR1_ARPE_MASK;
	TIM4.ccer |= TIMX_CCER_CC2E_MASK;
	TIM4.ccer |= TIMX_CCER_CC1E_MASK;
	TIM4.ccer |= TIMX_CCER_CC3E_MASK;
	TIM4.ccer |= TIMX_CCER_CC4E_MASK;
	TIM4.egr |= TIMX_EGR_UG_MASK;
	TIM4.psc = 9; /* With PLL, my OSCOPE shows this give a freq of 64.3 Hz. */
	TIM4.psc = 0;
#else
	regtim_simple_init_pwm(&TIM4, 0xff, 0);
	regtim_pwm_setall(&TIM4, 0x200, 0x60, 0xa000, 0x50);
#endif

	regtim_simple_init(&TIM5, 0x8FFF, 25);

	/* Add encoder input on TIM3, PB4/5 */
	/*
 *  Encoder configs:
 	TIMx_SMCR[SMS] = 1 to count TI2 edges
	TIMx_SMCR[SMS] = 2 to count TI1 edges
	TIMx_SMCR[SMS] = 3 to count both edges
	
	TIMx_CCER[CC1P] = polarity of TI1
	TIMx_CCER[CC2P] = polarity of TI2
	TIMx_CCER[CC1NP] = 0
	TIMx_CCER[CC2NP] = 0
	TIMx_ARR = range of counter
	*/
	GPB.mode |= (GPIO_MODE_MODE_ALTERNATE << (4 * 2));
	GPB.mode |= (GPIO_MODE_MODE_ALTERNATE << (5 * 2));
	gpio_setAltFunc(&GPB, 4, GPIO_AF_TIM_3_4_5);
	gpio_setAltFunc(&GPB, 5, GPIO_AF_TIM_3_4_5);

	regtim_simple_init_encoder(&TIM3, 256);

	GPB.mode |= (GPIO_MODE_MODE_GENERAL_OUTPUT << (6 * 2));
	GPB.od &= ~(1<<6);
	

#ifdef PB_MEANS_SKIP_PLL
	if ((GPA.id & GPIO_ID_ID_MASK(PUSHBUTTON))) {
		buttonState = 1;
		skipPll = 1;
	}
#endif
	setClock(skipPll);
	/* Redefine PUSBUTTON for encoder input, now that we're done using it for PLL switching */
	gpio_setAltFunc(&GPA, 0, GPIO_AF_TIM_1_2);
	gpio_setAltFunc(&GPB, 3, GPIO_AF_TIM_1_2);
	GPA.mode &= ~(GPIO_MODE_MASK << (0 * 2));
	GPA.mode |= (GPIO_MODE_MODE_ALTERNATE << (0 * 2));
	GPB.mode |= (GPIO_MODE_MODE_ALTERNATE << (3 * 2));

	regtim_simple_init_encoder(&TIM2, 256);
	TIM2.cnt = 0x7F; /* When using TIM2 as the setting for the ORANGE (LCD backlight) duty, start it midway so the screen won't look "off" */

	GPC.mode &= ~(GPIO_MODE_MASK << (6 * 2));
	GPC.mode |= (GPIO_MODE_MODE_ALTERNATE << (6 * 2));
	GPC.mode &= ~(GPIO_MODE_MASK << (7 * 2));
	GPC.mode |= (GPIO_MODE_MODE_ALTERNATE << (7 * 2));
	GPC.mode &= ~(GPIO_MODE_MASK << (8 * 2));
	GPC.mode |= (GPIO_MODE_MODE_ALTERNATE << (8 * 2));
	GPC.mode &= ~(GPIO_MODE_MASK << (9 * 2));
	GPC.mode |= (GPIO_MODE_MODE_ALTERNATE << (9 * 2));
	gpio_setAltFunc(&GPC, 6, GPIO_AF_TIM_8_9_10_11);
	gpio_setAltFunc(&GPC, 7, GPIO_AF_TIM_8_9_10_11);
	gpio_setAltFunc(&GPC, 8, GPIO_AF_TIM_8_9_10_11);
	gpio_setAltFunc(&GPC, 9, GPIO_AF_TIM_8_9_10_11);
	advtim_simple_init_pwm(&TIM8, 0x400, 200);
	regtim_pwm_setall(&TIM8, 0x1, 0x8, 0x3f, 0x3ff);

	/* TODO: For serial to RasbPi, use UART4 on pins PC10/PC11. Those are
	 * otherwise unused, and we can keep USART6 pins for timer input for
	 * second encoder wheel */
    GPC.mode &= ~(GPIO_MODE_MASK << (10 * 2));
    GPC.mode |= (GPIO_MODE_MODE_ALTERNATE << (10 * 2));
    GPC.mode &= ~(GPIO_MODE_MASK << (11 * 2));
    GPC.mode |= (GPIO_MODE_MODE_ALTERNATE << (11 * 2));
    gpio_setAltFunc(&GPC, 10, GPIO_AF_USART_4_5_6);
    gpio_setAltFunc(&GPC, 11, GPIO_AF_USART_4_5_6);

    RCC.apb1En |= RCC_APB1EN_UART4EN_MASK;
    usart_init(&UART4, getApb1Clock(), 9600);
    UART4.cr1 |= USART_CR1_RE_MASK; /* Enable reading */ /* TODO: Should be part of usart_init */
    usart_sendASynch(&UART4, 'r');
    usart_sendASynch(&UART4, '\r');
    usart_sendASynch(&UART4, '\n');

//	Set DBG_TIMx_STOP to adjust whether the timer continues on CPU halt.
	
	/* bcr:
	 * 19: CellRAM burst write enable (0=async, 1=sync)
	 * 15: AsyncWait
	 * 14: ExtMode
	 * 13: WaitEn
	 * 12: Write Enable
	 * 11: WaitCFG
	 * 10: WrapMode
	 * 9: Wait Pol
	 * 8: Burst En
	 * 6: FACCEN
	 * 5-4: MWID (0=8bit, 1=16bit, 2=rsvd, 3=rsvd)
	 * 3-2: MTYP (0=SRAM/ROM, 1=PSRAM, 2=NOR Flash/OneNAND flash, 3=rsvd)
	 * 1: MUXEN
	 * 0: MBKEN
	 */

	FSMC.bcr1 = (0 << 14) | 
		(1 << 12) |
		(0 << 6) |
		(1 << 4) |
		(0 << 2) |
		1;
	/* 29-28: ACCMOD (0=A, 1=B, 2=C, 3=D)
	 * 27-24: Data latency for synch burst NORFLASH (0=2, 0xf=17)
	 * 23-20: CLKDIV (0=rsvd, 1=2, 2=3, 0xf=16)
	 * 19-16: BUSTURN (delay at end of write/read)
	 * 15-8: DATAST data duration (0=rsvd, 1=1, 0xff = 255)
	 * 7-4: ADDHLD address hold (0=rsvd, 1=1, 0xf=15)
	 * 3-0: ADDSET address setup (0 = 0, 0xf = 1615
	 */
	FSMC.bwtr1 = (1 << 28) | (0xf << 24) | (0xf << 20) | (0xf << 16) | (0x00f << 8) | (7 << 4) | 7;
	FSMC.btr1 = (1 << 28) | (0xf << 24) | (0xf << 20) | (0xf << 16) | (0x00f << 8) | (7 << 4) | 7;

	uint32_t bcr1 = FSMC.bcr1;
	uint32_t brtr1 = FSMC.btr1;
	uint32_t bwtr1 = FSMC.bwtr1;

	uint32_t tim1,tim2;
	/* Reset LCD Controller */
	GPD.od &= ~(1 << 3);
	for (tim1 = 0; tim1 < 0x2; tim1++) {
		for (tim2 = 0; tim2 < 0xfffff; tim2++) {
			;
		}
	}
	GPD.od |= (1 << 3);


	lcdWriteReg(0x7, 0x21);
	lcdWriteReg(0x0, 0x1);
	lcdWriteReg(0x7, 0x23);
	lcdWriteReg(0x10, 0);
	int i;
	for (i = 0; i < 0xffff; i++) {
		;
	}
	lcdWriteReg(0x7, 0x33);
	// entry mode setting R11
	// driver ac settings R02
	lcdWriteReg(0x02, (1 << 12)); // | (1 << 10) | 3);
	lcdWriteReg(0x03, 0x3636);
	drawBackground();
	setLCDMode(LCD_MODE_COMMAND);
	writeLCD_raw(0x22);
	setLCDMode(LCD_MODE_DATA);

	dir = 1;
	i = 0;
	GPD.od &= ~(1 << 11);
	TIM3.cnt = 0;
	TIM8.cnt = 100;
	bool init = true;
	int clickcount = 0;
	const int clickcountinit = 2;
	while (true) {
		static uint16_t mc1, mc2, mc3;
		static uint16_t mcb1, mcb2, mcb3;
		static uint16_t t8v;
		static uint16_t last = 0xffff;
		static uint16_t lastb = 0xffff;
		static uint8_t b = 0;
		static uint16_t mybar = 0;
		static uint8_t lastread = 0;
		mc1 = TIM3.ccr1;
		mc2 = TIM3.ccr2;
		mc3 = TIM3.cnt;
		mcb1 = TIM2.ccr1;
		mcb2 = TIM2.ccr2;
		mcb3 = TIM2.cnt;
		t8v = TIM8.cnt;

		if (init || last != mc3) {
			/* First pass or encoder counter changed.
			 * Update bottom (raw encoder value) bar */
			last = mc3;
			lcdFillBox(20 + mc3, 20, 256 + 20, 60, RGB16(0,0,0x0f));
			lcdFillBox(20, 20, 20 + mc3, 60, RGB16(0,0x3f,0));
		}
		if (init || lastb != mcb3) {
			/* First pass or update second encoder. */
			lastb = mcb3;
			lcdFillBox(20 + mcb3, 200, 256 + 20, 220, RGB16(0,0,0x0f));
			lcdFillBox(20, 200, 20 + mcb3, 220, RGB16(0,0x3f,0));
			TIM4.ccr2 = mcb3; /* Brightness of ORANGE, AKA LCD Backlight. Range: 0 -- TIM4.arr */
			if (mcb3 < 128) {
				GPC.od &= ~(1 << 9);
			} else {
				GPC.od |= (1 << 9);
			}

		}

		if (init || (TIM5.sr & 1)) {
			static uint8_t tdiv = 0;
			/* First pass or update timer expired.*/
			TIM5.sr = 0;
			/* Stop the clicker if our countdown is done */
			if (clickcount > 0) {
				if (--clickcount == 0) {
					GPB.od &= ~(1 << 6);
				}
			}
			if (tdiv++ % 2 == 0) {
				b++;
				/* Update the timer bar */
				lcdFillBox(20 + b, 140, 256+20, 180, RGB16(0, 0, 0x0f));
				lcdFillBox(20, 140, 20 + b, 180, RGB16(0, 0x3f, 0));

				/* Figure out what our update is */
				if (init || (b % 2 == 0 && lastread != mc3)) {
					/* Encoder knob moved (and timer expired), so update
					 * our value using time to calculate acceleration.
					 */
					int delta = (int8_t)(mc3 - lastread);
					init = false;
					lastread = mc3;
					if (delta > 8 || delta < -8) {
						delta *= 20;
					} else if (delta > 6 || delta < -6) {
						delta *= 10;
					} else if (delta > 3 || delta < -3) {
						delta *= 3;
					}
					if (mybar + delta < 0) {
						mybar = 0;
					} else if (mybar + delta > 4 * 255) {
						mybar = 4 * 255;
					} else {
						mybar += delta;
					}
					uint8_t barval = (2 + mybar) / 4;
					lcdFillBox(20 + barval, 80, 256+20, 120, RGB16(0, 0, 0x0f));
					lcdFillBox(20, 80, 20 + barval, 120, RGB16(0, 0x3f, 0));

					//lcdFillBox(20 + mybar / 10, 200, 256+20, 220, RGB16(0, 0, 0));
					//lcdFillBox(20, 200, 20 + mybar/10, 220, RGB16(0x1f, 0x3f, 0x1f));
					static uint8_t lastbarval = 0;
					if (barval != lastbarval) {
						lastbarval = barval;
						GPB.od |= (1 << 6);
						clickcount = clickcountinit;
                        char buf[30];
                        sprintf(buf, "U %i\r\n", lastbarval);
                        int pos;
                        for (pos = 0; pos < strlen(buf); pos++) {
                            usart_sendASynch(&UART4, buf[pos]);
                        }
                    }
				}
			}
		}
        if ((GPA.id & GPIO_ID_ID_MASK(PUSHBUTTON)) && buttonState == 0) {
            buttonState = 1;
            char *buf = "P 30\r\n";
            int pos;
            for (pos = 0; pos < strlen(buf); pos++) {
                usart_sendASynch(&UART4, buf[pos]);
            }
        } else if ((GPA.id & GPIO_ID_ID_MASK(PUSHBUTTON)) == 0 && buttonState == 1) {
            buttonState = 0;
        }
        char cmd;
        if (usart_read(&UART4, &cmd)) {
            if (cmd == 'r') {
                mybar = 1;
                char *buf = "U 0\r\n";
                int pos;
                for (pos = 0; pos < strlen(buf); pos++) {
                    usart_sendASynch(&UART4, buf[pos]);
                }
            }
        }
	}
	return 0;
}

/* XXX This is not what _sbrk_r is supposed to look like, but it seems to
 * be enough to make sprintf work. */
static char storage[1024];
void *
_sbrk_r(void *p, uint32_t incr)
{
    return storage;
}
