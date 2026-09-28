#ifndef TEST_CH58X_COMMON_H
#define TEST_CH58X_COMMON_H

#include <stdint.h>

#define ENABLE             1U
#define RB_PIN_SPI0        2U
#define GPIO_Pin_13        0x2000U
#define GPIO_Pin_14        0x4000U
#define GPIO_ModeOut_PP_5mA 5U
#define Mode3_HighBitINFront 3U

uint32_t GetSysClock(void);
void GPIOPinRemap(uint8_t enable, uint16_t pin);
void GPIOB_ModeCfg(uint32_t pins, uint8_t mode);
void SPI0_MasterDefInit(void);
void SPI0_CLKCfg(uint8_t divider);
void SPI0_DataMode(uint8_t mode);
void SPI0_MasterDMATrans(uint8_t *data, uint16_t length);
void DelayMs(uint32_t milliseconds);

#endif /* TEST_CH58X_COMMON_H */
