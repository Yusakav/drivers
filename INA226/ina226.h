/**
 * @file ina226.h
 * @author nyarukov (luckychaoyue1@gmail.com)
 * @brief 
 * @version 0.1
 * @date 2026-01-12
 * 
 * @copyright Copyright (c) 2026
 * 
 */

#ifndef INA226_H
#define INA226_H

#include <stdint.h>


struct INA226_t 
{
    uint16_t volt;
    uint16_t curr;
    uint16_t power;
};


void ina226_init(struct INA226_t *ina);
void ina226_read(struct INA226_t *ina);


#endif /* INA226_H */
