#ifndef __BOOTLOADER_H
#define __BOOTLOADER_H

#include <stdint.h>

void Bootloader_Run(void);
uint8_t Bootloader_ShouldStayInLoader(void);

#endif
