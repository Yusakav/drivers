/**
 * @file st7735.c
 * @author nyarukov (luckychaoyue1@gmail.com)
 * @brief
 * @version 0.1
 * @date 2025-11-09
 *
 * @copyright Copyright (c) 2025
 *
 */

#include "st7735.h"

#define ST7735_NOP 0x00U                   /* No Operation: NOP                           */
#define ST7735_SW_RESET 0x01U              /* Software reset: SWRESET                     */
#define ST7735_READ_ID 0x04U               /* Read Display ID: RDDID                      */
#define ST7735_READ_STATUS 0x09U           /* Read Display Statu: RDDST                   */
#define ST7735_READ_POWER_MODE 0x0AU       /* Read Display Power: RDDPM                   */
#define ST7735_READ_MADCTL 0x0BU           /* Read Display: RDDMADCTL                     */
#define ST7735_READ_PIXEL_FORMAT 0x0CU     /* Read Display Pixel: RDDCOLMOD               */
#define ST7735_READ_IMAGE_MODE 0x0DU       /* Read Display Image: RDDIM                   */
#define ST7735_READ_SIGNAL_MODE 0x0EU      /* Read Display Signal: RDDSM                  */
#define ST7735_SLEEP_IN 0x10U              /* Sleep in & booster off: SLPIN               */
#define ST7735_SLEEP_OUT 0x11U             /* Sleep out & booster on: SLPOUT              */
#define ST7735_PARTIAL_DISPLAY_ON 0x12U    /* Partial mode on: PTLON                      */
#define ST7735_NORMAL_DISPLAY_OFF 0x13U    /* Partial off (Normal): NORON                 */
#define ST7735_DISPLAY_INVERSION_OFF 0x20U /* Display inversion off: INVOFF               */
#define ST7735_DISPLAY_INVERSION_ON 0x21U  /* Display inversion on: INVON                 */
#define ST7735_GAMMA_SET 0x26U             /* Gamma curve select: GAMSET                  */
#define ST7735_DISPLAY_OFF 0x28U           /* Display off: DISPOFF                        */
#define ST7735_DISPLAY_ON 0x29U            /* Display on: DISPON                          */
#define ST7735_CASET 0x2AU                 /* Column address set: CASET                   */
#define ST7735_RASET 0x2BU                 /* Row address set: RASET                      */
#define ST7735_WRITE_RAM 0x2CU             /* Memory write: RAMWR                         */
#define ST7735_RGBSET 0x2DU                /* LUT for 4k,65k,262k color: RGBSET           */
#define ST7735_READ_RAM 0x2EU              /* Memory read: RAMRD                          */
#define ST7735_PTLAR 0x30U                 /* Partial start/end address set: PTLAR        */
#define ST7735_TE_LINE_OFF 0x34U           /* Tearing effect line off: TEOFF              */
#define ST7735_TE_LINE_ON 0x35U            /* Tearing effect mode set & on: TEON          */
#define ST7735_MADCTL 0x36U                /* Memory data access control: MADCTL          */
#define ST7735_IDLE_MODE_OFF 0x38U         /* Idle mode off: IDMOFF                       */
#define ST7735_IDLE_MODE_ON 0x39U          /* Idle mode on: IDMON                         */
#define ST7735_COLOR_MODE 0x3AU            /* Interface pixel format: COLMOD              */
#define ST7735_FRAME_RATE_CTRL1 0xB1U      /* In normal mode (Full colors): FRMCTR1       */
#define ST7735_FRAME_RATE_CTRL2 0xB2U      /* In Idle mode (8-colors): FRMCTR2            */
#define ST7735_FRAME_RATE_CTRL3 0xB3U      /* In partial mode + Full colors: FRMCTR3      */
#define ST7735_FRAME_INVERSION_CTRL 0xB4U  /* Display inversion control: INVCTR           */
#define ST7735_DISPLAY_SETTING 0xB6U       /* Display function setting                    */
#define ST7735_PWR_CTRL1 0xC0U             /* Power control setting: PWCTR1               */
#define ST7735_PWR_CTRL2 0xC1U             /* Power control setting: PWCTR2               */
#define ST7735_PWR_CTRL3 0xC2U             /* In normal mode (Full colors): PWCTR3        */
#define ST7735_PWR_CTRL4 0xC3U             /* In Idle mode (8-colors): PWCTR4             */
#define ST7735_PWR_CTRL5 0xC4U             /* In partial mode + Full colors: PWCTR5       */
#define ST7735_VCOMH_VCOML_CTRL1 0xC5U     /* VCOM control 1: VMCTR1                      */
#define ST7735_VMOF_CTRL 0xC7U             /* Set VCOM offset control: VMOFCTR            */
#define ST7735_WRID2 0xD1U                 /* Set LCM version code: WRID2                 */
#define ST7735_WRID3 0xD2U                 /* Customer Project code: WRID3                */
#define ST7735_NV_CTRL1 0xD9U              /* NVM control status: NVCTR1                  */
#define ST7735_READ_ID1 0xDAU              /* Read ID1: RDID1                             */
#define ST7735_READ_ID2 0xDBU              /* Read ID2: RDID2                             */
#define ST7735_READ_ID3 0xDCU              /* Read ID3: RDID3                             */
#define ST7735_NV_CTRL2 0xDEU              /* NVM Read Command: NVCTR2                    */
#define ST7735_NV_CTRL3 0xDFU              /* NVM Write Command: NVCTR3                   */
#define ST7735_PV_GAMMA_CTRL 0xE0U         /* Set Gamma adjustment (+ polarity): GAMCTRP1 */
#define ST7735_NV_GAMMA_CTRL 0xE1U         /* Set Gamma adjustment (- polarity): GAMCTRN1 */
#define ST7735_EXT_CTRL 0xF0U              /* Extension command control                   */
#define ST7735_PWR_CTRL6 0xFCU             /* In partial mode + Idle mode: PWCTR6         */
#define ST7735_VCOM4_LEVEL 0xFFU           /* VCOM 4 level control  */

int32_t st7735_SetOrientation (uint32_t Orientation);

#define MODE 1

#include "soft_spi.h"

#if MODE == 0
static void my_spi_sck (uint8_t level) {

    GPIO_WriteBit (GPIOA, GPIO_Pin_5, level);
}

static void my_spi_mosi (uint8_t level) {
    GPIO_WriteBit (GPIOA, GPIO_Pin_7, level);
}

static void my_spi_cs (uint8_t level) {
    GPIO_WriteBit (GPIOA, GPIO_Pin_4, level);
}

static uint8_t my_spi_miso (void) {
    return 0;
}

static struct soft_spi_bus_t dev_spi = {
    .set_sck = my_spi_sck,
    .set_mosi = my_spi_mosi,
    .set_cs = my_spi_cs,
    .get_miso = my_spi_miso,
    .delay_us = Delay_Us,
    .half_period_us = 0};

#endif


void SPI1_SendByte_Only (u8 TxData) {
    // 1. 等待发送缓冲区为空
    while (SPI_I2S_GetFlagStatus (SPI1, SPI_I2S_FLAG_TXE) == RESET);

    // 2. 发送数据
    SPI_I2S_SendData (SPI1, TxData);

    // 3. 等待数据发送完成（必须等待 BSY 标志复位，确保位流全部移出）
    while (SPI_I2S_GetFlagStatus (SPI1, SPI_I2S_FLAG_BSY) == SET);
}

/**
 *
 * RST:PA3,CS:PA4,SCK:PA5,DC:PA6,MOSI:PA7
 *
 */
int32_t
st7735_write_reg (uint8_t cmd, uint8_t *data, uint32_t len) {
    GPIO_WriteBit (GPIOA, GPIO_Pin_15, 0);
    GPIO_WriteBit (GPIOC, GPIO_Pin_13, 0);
#if MODE == 1
    SPI1_SendByte_Only (cmd);
#else
    uint64_t cmd64 = cmd;
    spi_transfer (&dev_spi, SPI_MODE_3 | SPI_MSB_FIRST | SPI_CS_NONE | SPI_DATA_8BIT | SPI_CS_LOW,
                  &cmd64, NULL, 1);
#endif

    GPIO_WriteBit (GPIOC, GPIO_Pin_13, 1);
#if MODE == 1
    for (uint32_t i = 0; i < len; i++) {
        SPI1_SendByte_Only (data[i]);
    }
#else
    if (data && (len > 0))
        spi_transfer (&dev_spi, SPI_MODE_3 | SPI_MSB_FIRST | SPI_CS_NONE | SPI_DATA_8BIT | SPI_CS_LOW,
                      (uint64_t *)data, NULL, len);
#endif

    GPIO_WriteBit (GPIOA, GPIO_Pin_15, 1);

    return ST7735_OK;
}

int32_t
st7735_write_data (uint8_t *data, uint32_t len) {
    GPIO_WriteBit (GPIOA, GPIO_Pin_15, 0);
    GPIO_WriteBit (GPIOC, GPIO_Pin_13, 1);
#if MODE == 1
    for (uint32_t i = 0; i < len; i++) {
        SPI1_SendByte_Only (data[i]);
    }
#else
    if (data && (len > 0))
        spi_transfer (&dev_spi, SPI_MODE_3 | SPI_MSB_FIRST | SPI_CS_NONE | SPI_DATA_8BIT | SPI_CS_LOW,
                      (uint64_t *)data, NULL, len);
#endif

    GPIO_WriteBit (GPIOA, GPIO_Pin_15, 1);

    return ST7735_OK;
}

int32_t
st7735_reset (void) {
    GPIO_WriteBit (GPIOB, GPIO_Pin_4, 0);
    Delay_Ms (100);
    GPIO_WriteBit (GPIOB, GPIO_Pin_4, 1);
    Delay_Ms (120);

    return ST7735_OK;
}

int32_t
st7735_init (uint32_t ColorCoding, uint32_t Orientation) {
    uint8_t buf[20];
    uint8_t i = 0;

/*
MOSI    CLK     CS      DC      RST
PB5     PB3     PA15    PC13    PB4
*/
#if MODE == 1
    GPIO_InitTypeDef GPIO_InitStructure = {0};
    SPI_InitTypeDef SPI_InitStructure = {0};
    RCC_PB2PeriphClockCmd (RCC_PB2Periph_GPIOA |
                               RCC_PB2Periph_GPIOB |
                               RCC_PB2Periph_GPIOC |
                               RCC_PB2Periph_SPI1,
                           ENABLE);
                           
    GPIO_PinRemapConfig (GPIO_PartialRemap1_SPI1, ENABLE);

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_3;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init (GPIOB, &GPIO_InitStructure);

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_5;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init (GPIOB, &GPIO_InitStructure);

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_4;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init (GPIOB, &GPIO_InitStructure);

    
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_15;
    GPIO_Init (GPIOA, &GPIO_InitStructure);

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_13;
    GPIO_Init (GPIOC, &GPIO_InitStructure);

    GPIO_WriteBit (GPIOA, GPIO_Pin_15, 1);

    SPI_InitStructure.SPI_Direction = SPI_Direction_1Line_Tx;
    SPI_InitStructure.SPI_Mode = SPI_Mode_Master;
    SPI_InitStructure.SPI_DataSize = SPI_DataSize_8b;
    SPI_InitStructure.SPI_CPOL = SPI_CPOL_High;
    SPI_InitStructure.SPI_CPHA = SPI_CPHA_2Edge;
    SPI_InitStructure.SPI_NSS = SPI_NSS_Soft;
    SPI_InitStructure.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_2;
    SPI_InitStructure.SPI_FirstBit = SPI_FirstBit_MSB;
    SPI_InitStructure.SPI_CRCPolynomial = 7;
    SPI_Init (SPI1, &SPI_InitStructure);

    SPI_Cmd (SPI1, ENABLE);

#else
    GPIO_InitTypeDef GPIO_InitStructure =
        {
            .GPIO_Pin = GPIO_Pin_3 | GPIO_Pin_4 | GPIO_Pin_5 | GPIO_Pin_6 | GPIO_Pin_7,
            .GPIO_Mode = GPIO_Mode_Out_PP,
            .GPIO_Speed = GPIO_Speed_50MHz,
        };
    GPIO_Init (GPIOA, &GPIO_InitStructure);

    spi_bus_init (&dev_spi, SPI_MODE_3);
#endif
    st7735_reset();


    /* Out of sleep */
    st7735_write_reg (ST7735_SLEEP_OUT, NULL, 0);
    Delay_Ms (120);

    // st7735_write_reg (ST7735_DISPLAY_INVERSION_ON, NULL, 0);
    // st7735_write_reg (ST7735_DISPLAY_INVERSION_ON, NULL, 0);
    /* Frame rate ctrl - normal mode */
    i = 0;
    buf[i++] = 0x05;
    buf[i++] = 0x3A;
    buf[i++] = 0x3A;
    st7735_write_reg (ST7735_FRAME_RATE_CTRL1, buf, i);

    /* Frame rate ctrl - idle mode */
    i = 0;
    buf[i++] = 0x05;
    buf[i++] = 0x3A;
    buf[i++] = 0x3A;
    st7735_write_reg (ST7735_FRAME_RATE_CTRL2, buf, i);

    /* Frame rate ctrl - partial mode */
    i = 0;
    buf[i++] = 0x05;
    buf[i++] = 0x3A;
    buf[i++] = 0x3A;
    buf[i++] = 0x05;
    buf[i++] = 0x3A;
    buf[i++] = 0x3A;
    st7735_write_reg (ST7735_FRAME_RATE_CTRL3, buf, i);

    /* Display inversion ctrl */
    i = 0;
    buf[i++] = 0x03;
    st7735_write_reg (ST7735_FRAME_INVERSION_CTRL, buf, i);

    /* Power control 1 */
    i = 0;
    buf[i++] = 0x62;
    buf[i++] = 0x02;
    buf[i++] = 0x04;
    st7735_write_reg (ST7735_PWR_CTRL1, buf, i);

    /* Power control 2 */
    i = 0;
    buf[i++] = 0xC0;
    st7735_write_reg (ST7735_PWR_CTRL2, buf, i);

    /* Power control 3 */
    i = 0;
    buf[i++] = 0x0D;
    buf[i++] = 0x00;
    st7735_write_reg (ST7735_PWR_CTRL3, buf, i);

    /* Power control 4 */
    i = 0;
    buf[i++] = 0x8D;
    buf[i++] = 0x6A;
    st7735_write_reg (ST7735_PWR_CTRL4, buf, i);

    /* Power control 5 */
    i = 0;
    buf[i++] = 0x8D;
    buf[i++] = 0xEE;
    st7735_write_reg (ST7735_PWR_CTRL5, buf, i);

    /* VCOM control */
    i = 0;
    buf[i++] = 0x0E;
    st7735_write_reg (ST7735_VCOMH_VCOML_CTRL1, buf, i);

    /* Positive Gamma correction (PV Gamma) */
    i = 0;
    buf[i++] = 0x10;
    buf[i++] = 0x0E;
    buf[i++] = 0x02;
    buf[i++] = 0x03;
    buf[i++] = 0x0E;
    buf[i++] = 0x07;
    buf[i++] = 0x02;
    buf[i++] = 0x07;
    buf[i++] = 0x0A;
    buf[i++] = 0x12;
    buf[i++] = 0x27;
    buf[i++] = 0x37;
    buf[i++] = 0x00;
    buf[i++] = 0x0D;
    buf[i++] = 0x0E;
    buf[i++] = 0x10;
    st7735_write_reg (ST7735_PV_GAMMA_CTRL, buf, i);

    /* Negative Gamma correction (NV Gamma) */
    i = 0;
    buf[i++] = 0x10;
    buf[i++] = 0x0E;
    buf[i++] = 0x03;
    buf[i++] = 0x03;
    buf[i++] = 0x0F;
    buf[i++] = 0x06;
    buf[i++] = 0x02;
    buf[i++] = 0x08;
    buf[i++] = 0x0A;
    buf[i++] = 0x13;
    buf[i++] = 0x26;
    buf[i++] = 0x36;
    buf[i++] = 0x00;
    buf[i++] = 0x0D;
    buf[i++] = 0x0E;
    buf[i++] = 0x10;
    st7735_write_reg (ST7735_NV_GAMMA_CTRL, buf, i);

    /* Color mode */
    i = 0;
    buf[i++] = ColorCoding;
    st7735_write_reg (ST7735_COLOR_MODE, buf, i);

    i = 0;
    buf[i++] = 0x68;
    st7735_write_reg (ST7735_MADCTL, buf, i);


    /* Display ON */
    st7735_write_reg (ST7735_DISPLAY_ON, NULL, 0);


#if 0
    /* Frame rate ctrl - normal mode */
    i = 0;
    buf[i++] = 0x01;
    buf[i++] = 0x2C;
    buf[i++] = 0x2D;
    st7735_write_reg (ST7735_FRAME_RATE_CTRL1, buf, i);

    /* Frame rate ctrl - idle mode */
    i = 0;
    buf[i++] = 0x01;
    buf[i++] = 0x2C;
    buf[i++] = 0x2D;
    st7735_write_reg (ST7735_FRAME_RATE_CTRL2, buf, i);

    /* Frame rate ctrl - partial mode */
    i = 0;
    buf[i++] = 0x01;
    buf[i++] = 0x2C;
    buf[i++] = 0x2D;
    buf[i++] = 0x01;
    buf[i++] = 0x2C;
    buf[i++] = 0x2D;
    st7735_write_reg (ST7735_FRAME_RATE_CTRL3, buf, i);

    /* Display inversion ctrl */
    i = 0;
    buf[i++] = 0x07;
    st7735_write_reg (ST7735_FRAME_INVERSION_CTRL, buf, i);

    /* Power control 1 */
    i = 0;
    buf[i++] = 0xA2;
    buf[i++] = 0x02;
    buf[i++] = 0x84;
    st7735_write_reg (ST7735_PWR_CTRL1, buf, i);

    /* Power control 2 */
    i = 0;
    buf[i++] = 0xC5;
    st7735_write_reg (ST7735_PWR_CTRL2, buf, i);

    /* Power control 3 */
    i = 0;
    buf[i++] = 0x0A;
    buf[i++] = 0x00;
    st7735_write_reg (ST7735_PWR_CTRL3, buf, i);

    /* Power control 4 */
    i = 0;
    buf[i++] = 0x8A;
    buf[i++] = 0x2A;
    st7735_write_reg (ST7735_PWR_CTRL4, buf, i);

    /* Power control 5 */
    i = 0;
    buf[i++] = 0x8A;
    buf[i++] = 0xEE;
    st7735_write_reg (ST7735_PWR_CTRL5, buf, i);

    /* VCOM control */
    i = 0;
    buf[i++] = 0x0E;
    st7735_write_reg (ST7735_VCOMH_VCOML_CTRL1, buf, i);

    /* Display inversion OFF */
    st7735_write_reg (ST7735_DISPLAY_INVERSION_ON, NULL, 0);

    /* Color mode */
    i = 0;
    buf[i++] = ColorCoding; 
    st7735_write_reg (ST7735_COLOR_MODE, buf, i);

    /* Positive Gamma correction (PV Gamma) */
    i = 0;
    buf[i++] = 0x02;
    buf[i++] = 0x1C;
    buf[i++] = 0x07;
    buf[i++] = 0x12;
    buf[i++] = 0x37;
    buf[i++] = 0x32;
    buf[i++] = 0x29;
    buf[i++] = 0x2D;
    buf[i++] = 0x29;
    buf[i++] = 0x25;
    buf[i++] = 0x2B;
    buf[i++] = 0x39;
    buf[i++] = 0x00;
    buf[i++] = 0x01;
    buf[i++] = 0x03;
    buf[i++] = 0x10;
    st7735_write_reg (ST7735_PV_GAMMA_CTRL, buf, i);

    /* Negative Gamma correction (NV Gamma) */
    i = 0;
    buf[i++] = 0x03;
    buf[i++] = 0x1D;
    buf[i++] = 0x07;
    buf[i++] = 0x06;
    buf[i++] = 0x2E;
    buf[i++] = 0x2C;
    buf[i++] = 0x29;
    buf[i++] = 0x2D;
    buf[i++] = 0x2E;
    buf[i++] = 0x2E;
    buf[i++] = 0x37;
    buf[i++] = 0x3F;
    buf[i++] = 0x00;
    buf[i++] = 0x00;
    buf[i++] = 0x02;
    buf[i++] = 0x10;
    st7735_write_reg (ST7735_NV_GAMMA_CTRL, buf, i);

    /* Normal Display ON */
    st7735_write_reg (ST7735_NORMAL_DISPLAY_OFF, NULL, 0);

    /* Display ON */
    st7735_write_reg (ST7735_DISPLAY_ON, NULL, 0);

    st7735_SetOrientation(Orientation);
#endif
    return ST7735_OK;
}

int32_t
st7735_deinit (void) {

    return ST7735_OK;
}

void lcd_set_window (uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
    uint8_t i, buf[4];

    // Column address set (X)
    i = 0;
    buf[i++] = 0x00;
    buf[i++] = x0 + 1;
    buf[i++] = 0x00;
    buf[i++] = x1 + 1;
    st7735_write_reg (ST7735_CASET, buf, 4);

    // Row address set (Y)
    i = 0;
    buf[i++] = 0x00;
    buf[i++] = y0 + 1;
    buf[i++] = 0x00;
    buf[i++] = y1 + 1;
    st7735_write_reg (ST7735_RASET, buf, 4);

    // RAM write
    st7735_write_reg (ST7735_WRITE_RAM, NULL, 0);
}

static uint32_t OrientationTab[4][2] =
    {
        {0x40U, 0xC0U}, /* Portrait orientation choice of LCD screen               */
        {0x80U, 0x00U}, /* Portrait rotated 180? orientation choice of LCD screen  */
        {0x20U, 0x60U}, /* Landscape orientation choice of LCD screen              */
        {0xE0U, 0xA0U}  /* Landscape rotated 180? orientation choice of LCD screen */
};

int32_t st7735_SetOrientation (uint32_t Orientation) {
    int32_t ret;
    uint8_t tmp;
    uint32_t Width, Height;

    if ((Orientation == ST7735_ORIENTATION_PORTRAIT) || (Orientation == ST7735_ORIENTATION_PORTRAIT_ROT180)) {
        Width = ST7735_WIDTH;
        Height = ST7735_HEIGHT;
    } else {
        Width = ST7735_HEIGHT;
        Height = ST7735_WIDTH;
    }

    lcd_set_window (0U, 0U, Width, Height);

    tmp = (uint8_t)OrientationTab[0][1];
    ret += st7735_write_reg (ST7735_MADCTL, &tmp, 1);

    if (ret != ST7735_OK) {
        ret = ST7735_ERROR;
    }

    return ret;
}

void lcd_draw_point (uint16_t x, uint16_t y, uint16_t color) {
    uint8_t buf[2];
    buf[0] = color >> 8;
    buf[1] = color & 0xFF;

    lcd_set_window (x, y, x + 1, y + 1);
    st7735_write_data (buf, 2);  // 连续写 1 像素
}

void lcd_fill_rect (uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color) {
    uint32_t total = w * h;
    uint8_t buf[2];
    buf[0] = color >> 8;
    buf[1] = color & 0xFF;

    lcd_set_window (x, y, x + w - 1, y + h - 1);

    while (total--)
        st7735_write_data (buf, 2);
}

/**
 * @brief 画线函数
 *
 * @param x1 线起始点坐标X轴
 * @param y1 线起始点坐标Y轴
 * @param x2 线终止点坐标X轴
 * @param y2 线终止点坐标Y轴
 * @param color  颜色
 */
void lcd_draw_line (uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint32_t color) {
    uint16_t t;
    int xerr = 0, yerr = 0, delta_x, delta_y, distance;
    int incx, incy, uRow, uCol;
    delta_x = x2 - x1; /* 计算坐标增量 */
    delta_y = y2 - y1;
    uRow = x1;
    uCol = y1;
    if (delta_x > 0)       /* 设置单步方向 */
        incx = 1;
    else if (delta_x == 0) /* 垂直线 */
        incx = 0;
    else {
        incx = -1;
        delta_x = -delta_x;
    }
    if (delta_y > 0)
        incy = 1;
    else if (delta_y == 0) /* 水平线 */
        incy = 0;
    else {
        incy = -1;
        delta_y = -delta_y;
    }
    if (delta_x > delta_y) /*选取基本增量坐标轴  */
        distance = delta_x;
    else
        distance = delta_y;
    for (t = 0; t <= distance + 1; t++) /* 画线输出 */
    {

        lcd_draw_point (uRow, uCol, color); /* 画点 */
        xerr += delta_x;
        yerr += delta_y;
        if (xerr > distance) {
            xerr -= distance;
            uRow += incx;
        }
        if (yerr > distance) {
            yerr -= distance;
            uCol += incy;
        }
    }
}

/**
 * @brief       画矩形函数
 *
 * @param x1    矩形坐上角坐标X轴
 * @param y1    矩形坐上角坐标Y轴
 * @param x2    矩形右下角坐标X轴
 * @param y2    矩形右下角坐标Y轴
 * @param color
 */
void lcd_draw_rect (uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint32_t color) {
    lcd_draw_line (x1, y1, x2, y1, color);
    lcd_draw_line (x1, y1, x1, y2, color);
    lcd_draw_line (x1, y2, x2, y2, color);
    lcd_draw_line (x2, y1, x2, y2, color);
}

/**
 * @brief       在指定位置画一个指定大小的圆
 *
 * @param x0    圆心坐标X轴
 * @param y0    圆心坐标Y轴
 * @param r     圆形半径
 * @param color
 */
void lcd_draw_circle (uint16_t x0, uint16_t y0, uint16_t r, uint32_t color) {
    int mx = x0, my = y0;
    int x = 0, y = r;

    int d = 1 - r;
    while (y > x) /* y>x即第一象限的第1区八分圆 */
    {
        lcd_draw_point (x + mx, y + my, color);
        lcd_draw_point (y + mx, x + my, color);
        lcd_draw_point (-x + mx, y + my, color);
        lcd_draw_point (-y + mx, x + my, color);

        lcd_draw_point (-x + mx, -y + my, color);
        lcd_draw_point (-y + mx, -x + my, color);
        lcd_draw_point (x + mx, -y + my, color);
        lcd_draw_point (y + mx, -x + my, color);
        if (d < 0) {
            d = d + 2 * x + 3;
        } else {
            d = d + 2 * (x - y) + 5;
            y--;
        }
        x++;
    }
}

/**
 * @brief 绘制填充圆角矩形
 * @param x, y  起始坐标
 * @param w, h  宽度和高度
 * @param r     圆角半径 (建议取 2 或 3)
 * @param color 颜色
 */
void lcd_fill_round_rect (uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t r, uint16_t color) {
    if (w < 2 * r)
        return;

    // 1. 绘制中间的主体部分（去掉了四个角）
    lcd_fill_rect (x + r, y, w - 2 * r, h, color);          // 横向中间块
    lcd_fill_rect (x, y + r, r, h - 2 * r, color);          // 左侧中间块
    lcd_fill_rect (x + w - r, y + r, r, h - 2 * r, color);  // 右侧中间块

    // 左上角点
    lcd_draw_point (x + 1, y + 1, color);
    // 右上角点
    lcd_draw_point (x + w - 2, y + 1, color);
    // 左下角点
    lcd_draw_point (x + 1, y + h - 2, color);
    // 右下角点
    lcd_draw_point (x + w - 2, y + h - 2, color);
}

/**
 * @brief 绘制圆角边框 (用于进度条底框)
 */
void lcd_draw_round_rect (uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t r, uint16_t color) {
    // 绘制上下左右四条边，边缘缩进 r
    lcd_draw_line (x + r, y, x + w - r, y, color);                  // 上边
    lcd_draw_line (x + r, y + h - 1, x + w - r, y + h - 1, color);  // 下边
    lcd_draw_line (x, y + r, x, y + h - r, color);                  // 左边
    lcd_draw_line (x + w - 1, y + r, x + w - 1, y + h - r, color);  // 右边

    // 补上四个斜角点
    lcd_draw_point (x + 1, y + 1, color);
    lcd_draw_point (x + w - 2, y + 1, color);
    lcd_draw_point (x + 1, y + h - 2, color);
    lcd_draw_point (x + w - 2, y + h - 2, color);
}

#include "ascii_font.h"

/**
 * @brief 在指定位置显示一个字符
 *
 * @param x 起始坐标X轴
 * @param y 起始坐标Y轴
 * @param num 显示字符
 * @param size 字体大小, 可选12/16/24/32
 * @param color 字体颜色
 */
void lcd_show_char (uint16_t x, uint16_t y, uint8_t num, uint8_t size, uint32_t color) {
    uint8_t mode = 1;
    uint8_t temp, t1, t;
    uint16_t y0 = y;
    uint8_t csize = (size / 8 + ((size % 8) ? 1 : 0)) * (size / 2); /* 得到字体一个字符对应点阵集所占的字节数	 */
    num = num - ' ';                                                /*得到偏移后的值（ASCII字库是从空格开始取模，所以-' '就是对应字符的字库）  */
    for (t = 0; t < csize; t++) {
        if (size == 12)
            temp = asc2_1206[num][t]; /* 调用1206字体 */
        else if (size == 16)
            temp = asc2_1608[num][t]; /* 调用1608字体 */
        else if (size == 24)
            temp = asc2_2412[num][t]; /* 调用2412字体 */
        else if (size == 32)
            temp = asc2_3216[num][t]; /* 调用3216字体 */
        else
            return;                   /* 没有的字库 		*/
        for (t1 = 0; t1 < 8; t1++) {
            if (temp & 0x80)
                lcd_draw_point (x, y, color);
            else if (mode == 0) {
                // lcd_draw_point (x, y, 0x0000);
            }
            temp <<= 1;
            y++;
            if (y >= 600)
                return; /* 超区域了 */
            if ((y - y0) == size) {
                y = y0;
                x++;
                break;
            }
        }
    }
}

/**
 * @brief 显示一串字符串
 *
 * @param x         起始坐标点X轴。
 * @param y         起始坐标点Y轴。
 * @param width     字符串显示区域长度
 * @param height    字符串显示区域高度
 * @param size      字体大小
 * @param p         要显示的字符串首地址
 * @param color
 */
void lcd_show_string (uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint8_t size, char *p, uint32_t color) {
    uint8_t x0 = x;
    width += x;
    height += y;
    while ((*p <= '~') && (*p >= ' ')) /* 判断是不是非法字符! */
    {
        if (x >= width) {
            x = x0;
            y += size;
        }
        if (y >= height)
            break; /* 退出 */
        lcd_show_char (x, y, *p, size, color);
        x += size / 2;
        p++;
    }
}

/**
 * @brief 计算m的n次方
 *
 * @param m 要计算的值
 * @param n n次方
 * @return uint32_t m^n次方.
 */
uint32_t lcd_pow (uint8_t m, uint8_t n) {
    uint32_t result = 1;
    while (n--)
        result *= m;
    return result;
}

/**
 * @brief 显示指定的数字，高位为0的话不显示
 *
 * @param x         起始坐标点X轴。
 * @param y         起始坐标点Y轴。
 * @param num       数值
 * @param len       数字位数。
 * @param size      字体大小
 * @param color
 */
void lcd_show_num (uint16_t x, uint16_t y, uint32_t num, uint8_t len, uint8_t size, uint32_t color) {
    uint8_t t, temp;
    uint8_t enshow = 0;
    for (t = 0; t < len; t++) {
        temp = (num / lcd_pow (10, len - t - 1)) % 10;
        if (enshow == 0 && t < (len - 1)) {
            // if (temp == 0) {
            //     lcd_show_char (x + (size / 2) * t, y, ' ', size, color);
            //     continue;
            // } else
            enshow = 1;
        }
        lcd_show_char (x + (size / 2) * t, y, temp + '0', size, color);
    }
}