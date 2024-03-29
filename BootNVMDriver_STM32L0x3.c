/*
 *  Created on: Oct 24, 2023
 *  Author: BalazsFarkas
 *  Project: STM32_Bootloader
 *  Processor: STM32L053R8
 *  Program version: 1.0
 *  File: BootNVMDriver_STM32L0x3.c
 *  Modified from: STM32_NVMDriver/NVMDriver_STM32L0x3.c
 *  Change history:
 *
 *  Code holds the NVM management functions used within the bootloader.
 *
 * v.1.0
 * Slightly rework version of the previously written NVM driver code.
 *
 */

#include <BootNVMDriver_STM32L0x3.h>
#include "main.h"


//1)FLASH speed and interrupt initialisation
void NVM_Init (void){
	/*
	 * Function to set NVM functions (FLASH, EEPROM and Option bytes).
	 * We generally don't need to use this since FLASH is already properly initialised upon startup and we don't use EEPROM.
	 * Separate NVMs should be interacted with separately in code.
	 * Note: even though all registers are called FLASH, it is actually NVM and not just FLASH.
	 * Note: in EEPROM, a page and the word are the same size.
	 *
	 * 1)Unlock NVMs
	 * 2)Set speed and buffers
	 * 3)Set interrupts
	 * 4)Close the PELOCK
	 *
	 * */

	//1)
	FLASH->PEKEYR = 0x89ABCDEF;					//PEKEY1
	FLASH->PEKEYR = 0x02030405;					//PEKEY2
												//Note: NVM has a two step enable element to unlock the PECR register and put PELOCK to 0

	//2)
	//FLASH.ACR register modification comes here when necessary.
	//It sets up the speed, latency and the preread of the NVMs.


	//3)
	FLASH->PECR &= ~(1<<16);					//EOP interrupt disabled (EOPIE)
												//Note: since we do FLASH writing word by word, this interrupt will be mostly useless.
	FLASH->PECR |= (1<<17);						//Error interrupt enabled (ERRIE)
//	FLASH->PECR |= (1<<23);						//we would enable the NZDISABLE erase check (will only allow writing to FLASH if FLASH has been erased)
												//on L0xx devices, it doesn't seem to exist

	//4)
//	FLASH->OPTR = (0xB<<0);						//we switch to Level 1 protection using RDPROT bits.
												//Usually Level 1 protection is fine and should not be changed.
												//Note: writing 0xCC to the RDPORT puts Level 2 protection, which bricks the micro indefinitely!!!
	//5)
	FLASH->PECR |= (1<<0);						//we set PELOCK on the NVM to 1, locking it again for writing operations
}

//2)Erase a page of FLASH
void FLASHErase_Page(uint32_t flash_page_addr) {
	/* This function erases a full page of NVM. A page consists of 8 rows of 4 words (128 bytes or 1 kbit).
	 * It is not possible to erase a smaller section of FLASH than a page.
	 *
	 * 1)Unlock the NVM control register PECR.
	 * 2)Unlock FLASH memory.
	 * 3)Remove readout protection (if necessary)
	 * 4)Choose the erase action. Pick the FLASH as the target of the operation.
	 * 5)Replace the word with the new one and wait until success flag is raised
	 * 6)Close NVM and add readout protection
	 *
	 * Note: writing 0xCC to the RDPORT bricks the micro indefinitely!!!
	 */

	//1)
	FLASH->PEKEYR = 0x89ABCDEF;					//PEKEY1
	FLASH->PEKEYR = 0x02030405;					//PEKEY2

	//2)
	FLASH->PRGKEYR = 0x8C9DAEBF;				//RRGKEY1
	FLASH->PRGKEYR = 0x13141516;				//RRGKEY2

	//3)
//	FLASH->OPTR = (0xAA<<0);					//we switch to Level 0 protection using RDPROT bits

	//4)
	FLASH->PECR |= (1<<9);						//we ERASE
	FLASH->PECR |= (1<<3);						//we pick the FLASH for erasing

	//5)
	*(__IO uint32_t*)(flash_page_addr) = (uint32_t)0;		//value doesn't actually matter here, we are erasing

	while((FLASH->SR & (1<<0)) == (1<<0));		//we stay in the loop while the BSY flag is 1

	while(!(((FLASH->SR & (1<<1)) == (1<<1))));	//we stay in the loop while the EOP flag is not 1
	FLASH->SR |= (1<<1);						//we reset the EOP flag to 0 by writing 1 to it

	//6)
//	FLASH->OPTR = (0xBB<<0);					//we switch back to Level 1 protection using RDPROT bits
	FLASH->PECR |= (1<<0);						//we set PELOCK on the NVM to 1, locking it again for writing operations
}

//3)Write a word to a FLASH address
void FLASHUpd_Word(uint32_t flash_word_addr, uint32_t updated_flash_value) {
	/* This function writes a 32-bit word in the NVM.
	 * The code assumes that the target position is empty. If it is not, the resulting word will be corrupted (a bitwise OR of the original value and the new one).
	 * On L0xx, there is no NOTZEROERR control to avoid this corruption.
	 *
	 * 1)We do an endian swap on the input data (the FLASH will publish the data in an endian inverted to the code)
	 * 		Note: if the transmission of the machine code already does an endian swap, this step is not necessary.
	 * 2)Unlock the NVM control register PECR.
	 * 3)Unlock FLASH memory.
	 * 4)Remove readout protection (if necessary)
	 * 5)Replace the word with the new one and wait until success flag is raised
	 * 			Note: NOTZEROERR flag/interrupt may not be available on certain devices, meaning that data will be written to a target independent of what is already there.
	 * 			Note: if NOTZEROERR flag/interrupt is active, only if the target is empty are we allowed to write there.
	 * 6)Close NVM and add readout protection
	 *
	 * Note: writing is a bitwise "OR" operation. Target must be erased first (see FLASHErase_Page function).
	 * Note: the arriving byte sequence is LSB byte first, not MSB byte first. The machine code within the micro is flipped compared to what is loaded into it.
	 * Note: writing 0xCC to the RDPORT bricks the micro indefinitely!!!
	 */

	//1)
#ifdef endian_swap
	uint32_t swapped_updated_flash_value = ((updated_flash_value >> 24) & 0xff) | 		// move byte 3 to byte 0
	                    ((updated_flash_value << 8) & 0xff0000) | 						// move byte 1 to byte 2
	                    ((updated_flash_value >> 8) & 0xff00) | 						// move byte 2 to byte 1
	                    ((updated_flash_value << 24) & 0xff000000); 					// byte 0 to byte 3
#endif

	//2)
	FLASH->PEKEYR = 0x89ABCDEF;					//PEKEY1
	FLASH->PEKEYR = 0x02030405;					//PEKEY2

	//3)
	FLASH->PRGKEYR = 0x8C9DAEBF;				//RRGKEY1
	FLASH->PRGKEYR = 0x13141516;				//RRGKEY2
												//Note: FLASH has a two step enable element to unlock writing to the FLASH
												//Note: PRGLOCK bits being 0 is a precondition for writing to FLASH
												//Note: PELOCK is already removed in step 1), using the PEKEY

	//4)
//	FLASH->OPTR = (0xAA<<0);					//we switch to Level 0 protection using RDPROT bits
												//in-application FLASH should be modified at Level1 readout protection, so likley no need to change that

	//5)
	*(__IO uint32_t*)(flash_word_addr) = updated_flash_value;

#ifdef endian_swap
	//*(__IO uint32_t*)(flash_word_addr) = swapped_updated_flash_value;
#endif

												//Note: the target area must be erased before writing to it, otherwise data gets corrupted

	while((FLASH->SR & (1<<0)) == (1<<0));		//we stay in the loop while the BSY flag is 1
	while(!(((FLASH->SR & (1<<1)) == (1<<1))));	//we stay in the loop while the EOP flag is not 1
	FLASH->SR |= (1<<1);						//we reset the EOP flag to 0 by writing 1 to it

	//6)
//	FLASH->OPTR = (0xBB<<0);					//we switch back to Level 1 protection using RDPROT bits
	FLASH->PECR |= (1<<0);						//we set PELOCK on the NVM to 1, locking it again for writing operations
}




//4)Write a half-page to a FLASH address
void FLASHUpd_HalfPage(uint32_t loc_var_current_flash_half_page_addr, uint8_t full_page_cnt_in_buf, uint8_t half_page_cnt_in_page) {
	/*

	 * The function MUST run in RAM, not in FLASH!!!!!!
	 * Call with the __RAM_FUNC attribute!!!!!
	 *
	 * Also, ALL IRQs must be disabled during the write process or we have a crash.
	 *
	 * This function writes a sixteen 32-bit words in the NVM.
	 * The address of the action must align to a half page - first 6 bits of the first address must be 0.
	 * The code assumes that the target position is empty. If it is not, the resulting word will be corrupted (a bitwise OR of the original value and the new one).
	 * On L0xx, there is no NOTZEROERR control to avoid this corruption.
	 *
	 * We are using the function by relying on local variables. Stepping (half-page selection and page selection) is done externally.
	 *
	 * //Note: we remain within the same half-page on this level
	 *
	 * 1)Unlock the NVM control register PECR.
	 * 2)Unlock FLASH memory.
	 * 3)Remove readout protection (if necessary)
	 * 4)We pick FLASH programming at half-page.
	 * 5)Disable IRQs
	 * 6)Replace the word with the new one and wait until success flag is raised
	 * 			Note: NOTZEROERR flag/interrupt may not be available on certain devices, meaning that data will be written to a target independent of what is already there.
	 * 			Note: if NOTZEROERR flag/interrupt is active, only if the target is empty are we allowed to write there.
	 * 7)Close NVM and add readout protection
	 * 8)Enable IRQs
	 *
	 * Note: writing is a bitwise "OR" operation. Target must be erased first (see FLASHErase_Page function).
	 * Note: the arriving byte sequence is LSB byte first, not MSB byte first. The machine code within the micro is flipped compared to what is loaded into it.
	 * Note: writing 0xCC to the RDPORT bricks the micro indefinitely!!!
	 */

	//1)
	FLASH->PEKEYR = 0x89ABCDEF;					//PEKEY1
	FLASH->PEKEYR = 0x02030405;					//PEKEY2

	//2)
	FLASH->PRGKEYR = 0x8C9DAEBF;				//RRGKEY1
	FLASH->PRGKEYR = 0x13141516;				//RRGKEY2
												//Note: FLASH has a two step enable element to unlock writing to the FLASH
												//Note: PRGLOCK bits being 0 is a precondition for writing to FLASH
												//Note: PELOCK is already removed in step 1), using the PEKEY

	//3)
//	FLASH->OPTR = (0xAA<<0);					//we switch to Level 0 protection using RDPROT bits
												//in-application FLASH should be modified at Level1 readout protection, so likley no need to change that

	//4)
	FLASH->PECR |= (1<<3);						//we pick the FLASH for programming (PRG)
	FLASH->PECR |= (1<<10);						//we pick the half-page programming mode (FPPRG)


	//5)
	__disable_irq();							//we disable all the IRQs
												//Note: apparently this was "forgotten" in the refman, but one must deactivate all IRQs before working with FLASH, otherwise the writing will be interrupted
												//Note: it actually makes complete sense...a pickle it is not mentioned whatsoever

	//6)
	for(uint8_t i = 0; i < 16; i++) {
		*(__IO uint32_t*)(loc_var_current_flash_half_page_addr) = Rx_Message_buf[(32 * full_page_cnt_in_buf) + (16 * half_page_cnt_in_page) + i];
												//Note: the half page address does not need to be changed (similar to the erasing command)
												//Note: we only need to step the pointer for the data we want to write into the FLASH
	}

	while((FLASH->SR & (1<<0)) == (1<<0));		//we stay in the loop while the BSY flag is 1
	while(!(((FLASH->SR & (1<<1)) == (1<<1))));	//we stay in the loop while the EOP flag is not 1
												//EOP will go HIGH only after the 16 words have been copied properly
	FLASH->SR |= (1<<1);						//we reset the EOP flag to 0 by writing 1 to it

	//7)
	FLASH->PECR &= ~(1<<3);						//we disable the FLASH for programming
	FLASH->PECR &= ~(1<<10);					//we disable the half-page programming mode
	FLASH->PECR |= (1<<0);						//we set PELOCK on the NVM to 1, locking it again for writing operations

	//8)
	__enable_irq();								//we re-enable the IRQs
}




//5)
//if we encounter an error during writing to the FLASH, the code stops working
void FLASH_IRQHandler(void){
	printf("Memory error... \r\n");
	FLASH->SR |= (0x32F<<8);						//we reset all the error interrupt flags
	while(1);
}


//6)FLASH IRQ priority
void FLASHIRQPriorEnable(void) {
	NVIC_SetPriority(FLASH_IRQn, 1);
	NVIC_EnableIRQ(FLASH_IRQn);
}
