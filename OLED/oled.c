#include "oled.h"
#include "stdlib.h"
#include "oledfont.h"
#include "main.h"

//CH1115驱动芯片参数 (0.5寸 88x48 OLED)
#define OLED_WIDTH    88
#define OLED_HEIGHT   48
#define OLED_PAGES    6
#define OLED_ADDR     0x78

uint8_t OLED_GRAM[OLED_WIDTH][OLED_PAGES];
uint8_t x_low_offset;
uint8_t x_high_offset;
uint8_t display_angle = 0;  // 0=0°, 1=90°, 2=180°, 3=270°
uint8_t y_offset = 10;  // 已配 0x40 起始行 + 0xD3 偏移0，无需额外像素偏移

//颜色反转
void OLED_ColorTurn(uint8_t i)
{
	if(i==0)
	{
		OLED_WR_Byte(0xA6,OLED_CMD);
	}
	if(i==1)
	{
		OLED_WR_Byte(0xA7,OLED_CMD);
	}
}

//屏幕旋转 0=0°, 1=90°, 2=180°, 3=270°
void OLED_DisplayTurn(uint8_t angle)
{
	display_angle = angle;
	x_low_offset = 0x00;
	x_high_offset = 0x00;
	OLED_WR_Byte(0xA0, OLED_CMD);
	OLED_WR_Byte(0xC0, OLED_CMD);
}

//I2C延时：12MHz 下约 5us，位周期约 15us ≈ 66kHz（CH1115 上限 400kHz）
void IIC_delay(void)
{
	volatile uint32_t t = 12;
	while(t--);
}

//起始信号
void I2C_Start(void)
{
	OLED_SDA_Set();
	OLED_SCL_Set();
	IIC_delay();
	IIC_delay();
	OLED_SDA_Clr();
	IIC_delay();
	IIC_delay();
	OLED_SCL_Clr();
	IIC_delay();
}

//停止信号
void I2C_Stop(void)
{
	OLED_SDA_Clr();
	OLED_SCL_Set();
	IIC_delay();
	IIC_delay();
	OLED_SDA_Set();
	IIC_delay();
	IIC_delay();
}

//等待应答 (SCL 拉高期间读 SDA；返回1=无应答 NACK，0=应答 ACK)
uint8_t I2C_WaitAck(void)
{
	uint8_t nack;
	OLED_SDA_Set();
	IIC_delay();
	OLED_SCL_Set();
	IIC_delay();
	nack = ((OLED_SDA_Port->IDR & OLED_SDA_Pin) == OLED_SDA_Pin) ? 1 : 0;
	OLED_SCL_Clr();
	IIC_delay();
	return nack;
}

//发送一字节 (MSB 先出)
void Send_Byte(uint8_t dat)
{
	uint8_t i;
	for(i=0;i<8;i++)
	{
		if(dat & 0x80)
			OLED_SDA_Set();
		else
			OLED_SDA_Clr();
		IIC_delay();
		OLED_SCL_Set();
		IIC_delay();
		OLED_SCL_Clr();
		IIC_delay();
		dat <<= 1;
	}
}

//探测OLED是否在线 (返回1=有应答)
uint8_t OLED_Probe(void)
{
	uint8_t nack;
	I2C_Start();
	Send_Byte(OLED_ADDR);
	nack = I2C_WaitAck();
	I2C_Stop();
	return (nack == 0) ? 1 : 0;
}

//写一字节 (0x00=命令, 0x40=数据)
void OLED_WR_Byte(uint8_t dat, uint8_t mode)
{
	I2C_Start();
	Send_Byte(OLED_ADDR);
	if(I2C_WaitAck()) { I2C_Stop(); return; }
	Send_Byte(mode ? 0x40 : 0x00);
	if(I2C_WaitAck()) { I2C_Stop(); return; }
	Send_Byte(dat);
	I2C_WaitAck();
	I2C_Stop();
}

//打开OLED显示
void OLED_DisPlay_On(void)
{
	OLED_WR_Byte(0x8D, OLED_CMD);
	OLED_WR_Byte(0x14, OLED_CMD);
	OLED_WR_Byte(0xAF, OLED_CMD);
}

//关闭OLED显示
void OLED_DisPlay_Off(void)
{
	OLED_WR_Byte(0x8D, OLED_CMD);
	OLED_WR_Byte(0x10, OLED_CMD);
	OLED_WR_Byte(0xAE, OLED_CMD);
}

//刷新显示
void OLED_Refresh(void)
{
	uint8_t i, n;
	for(i = 0; i < OLED_PAGES; i++)
	{
		OLED_WR_Byte(0xB0 + i, OLED_CMD);
		OLED_WR_Byte(0x00 + x_low_offset, OLED_CMD);
		OLED_WR_Byte(0x10 + x_high_offset, OLED_CMD);

		I2C_Start();
		Send_Byte(OLED_ADDR);
		I2C_WaitAck();
		Send_Byte(0x40);
		I2C_WaitAck();
		for(n = 0; n < OLED_WIDTH; n++)
		{
			Send_Byte(OLED_GRAM[n][i]);
			I2C_WaitAck();
		}
		I2C_Stop();
	}
}

//清屏
void OLED_Clear(void)
{
	uint8_t i, n;
	for(i = 0; i < OLED_PAGES; i++)
	{
		for(n = 0; n < OLED_WIDTH; n++)
		{
			OLED_GRAM[n][i] = 0;
		}
	}
	OLED_Refresh();
}

//画点 (支持4向软件旋转)
void OLED_DrawPoint(uint8_t x, uint8_t y, uint8_t t)
{
	uint8_t i, m, n, rx, ry;

	// 逻辑坐标转物理坐标 (物理空间始终 88x48)
	switch(display_angle)
	{
		case 0: // 0°: 88x48 逻辑
			rx = x;
			ry = y;
			break;
		case 1: // 90° CW: 48x88 逻辑 -> 88x48 物理
			rx = y;
			ry = (OLED_WIDTH - 1 - x);
			break;
		case 2: // 180°: 88x48 逻辑
			rx = (OLED_WIDTH - 1 - x);
			ry = (OLED_HEIGHT - 1 - y);
			break;
		case 3: // 270° CW: 48x88 逻辑 -> 88x48 物理
			rx = (OLED_HEIGHT - 1 - y);
			ry = x;
			break;
		default:
			rx = x;
			ry = y;
			break;
	}

	// 添加硬件像素偏移
	ry += y_offset;

	// 物理坐标边界检查
	if(rx >= OLED_WIDTH || ry >= OLED_HEIGHT) return;

	i = ry / 8;
	m = ry % 8;
	n = 1 << m;

	if(t)
	{
		OLED_GRAM[rx][i] |= n;
	}
	else
	{
		OLED_GRAM[rx][i] &= ~n;
	}
}

//画线
void OLED_DrawLine(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2, uint8_t mode)
{
	uint16_t t;
	int xerr = 0, yerr = 0, delta_x, delta_y, distance;
	int incx, incy, uRow, uCol;

	delta_x = x2 - x1;
	delta_y = y2 - y1;
	uRow = x1;
	uCol = y1;

	if(delta_x > 0) incx = 1;
	else if(delta_x == 0) incx = 0;
	else { incx = -1; delta_x = -delta_x; }

	if(delta_y > 0) incy = 1;
	else if(delta_y == 0) incy = 0;
	else { incy = -1; delta_y = -delta_y; }

	if(delta_x > delta_y) distance = delta_x;
	else distance = delta_y;

	for(t = 0; t < distance + 1; t++)
	{
		OLED_DrawPoint(uRow, uCol, mode);
		xerr += delta_x;
		yerr += delta_y;
		if(xerr > distance)
		{
			xerr -= distance;
			uRow += incx;
		}
		if(yerr > distance)
		{
			yerr -= distance;
			uCol += incy;
		}
	}
}

//画圆
void OLED_DrawCircle(uint8_t x, uint8_t y, uint8_t r)
{
	int a, b, num;
	a = 0;
	b = r;
	while(2 * b * b >= r * r)
	{
		OLED_DrawPoint(x + a, y - b, 1);
		OLED_DrawPoint(x - a, y - b, 1);
		OLED_DrawPoint(x - a, y + b, 1);
		OLED_DrawPoint(x + a, y + b, 1);
		OLED_DrawPoint(x + b, y + a, 1);
		OLED_DrawPoint(x + b, y - a, 1);
		OLED_DrawPoint(x - b, y - a, 1);
		OLED_DrawPoint(x - b, y + a, 1);

		a++;
		num = (a * a + b * b) - r * r;
		if(num > 0)
		{
			b--;
			a--;
		}
	}
}

//显示单个字符
void OLED_ShowChar(uint8_t x, uint8_t y, uint8_t chr, uint8_t size1, uint8_t mode)
{
	uint8_t i, m, temp, size2, chr1;
	uint8_t x0 = x, y0 = y;

	if(size1 == 8) size2 = 6;
	else size2 = (size1 / 8 + ((size1 % 8) ? 1 : 0)) * (size1 / 2);

	chr1 = chr - ' ';

	for(i = 0; i < size2; i++)
	{
		if(size1 == 8)
			temp = asc2_0806[chr1][i];
		else if(size1 == 12)
			temp = asc2_1206[chr1][i];
		else if(size1 == 16)
			temp = asc2_1608[chr1][i];
		else if(size1 == 24)
			temp = asc2_2412[chr1][i];
		else return;

		for(m = 0; m < 8; m++)
		{
			if(temp & 0x01) OLED_DrawPoint(x, y, mode);
			else OLED_DrawPoint(x, y, !mode);
			temp >>= 1;
			y++;
		}
		x++;
		if((size1 != 8) && ((x - x0) == size1 / 2))
		{ x = x0; y0 = y0 + 8; }
		y = y0;
	}
}

//显示字符串
void OLED_ShowString(uint8_t x, uint8_t y, char *chr, uint8_t size1, uint8_t mode)
{
	while((*chr >= ' ') && (*chr <= '~'))
	{
		OLED_ShowChar(x, y, *chr, size1, mode);
		if(size1 == 8) x += 6;
		else x += size1;
		chr++;
	}
}

//幂运算
uint32_t OLED_Pow(uint8_t m, uint8_t n)
{
	uint32_t result = 1;
	while(n--)
	{
		result *= m;
	}
	return result;
}

//显示数字
void OLED_ShowNum(uint8_t x, uint8_t y, uint32_t num, uint8_t len, uint8_t size1, uint8_t mode)
{
	uint8_t t, temp, m = 0;
	if(size1 == 8) m = 2;

	for(t = 0; t < len; t++)
	{
		temp = (num / OLED_Pow(10, len - t - 1)) % 10;
		if(temp == 0)
		{
			OLED_ShowChar(x + (size1 / 2 + m) * t, y, '0', size1, mode);
		}
		else
		{
			OLED_ShowChar(x + (size1 / 2 + m) * t, y, temp + '0', size1, mode);
		}
	}
}

//显示汉字
void OLED_ShowChinese(uint8_t x, uint8_t y, uint8_t num, uint8_t size1, uint8_t mode)
{
	uint8_t m, temp;
	uint8_t x0 = x, y0 = y;
	uint16_t i, size3;

	size3 = (size1 / 8 + ((size1 % 8) ? 1 : 0)) * size1;

	for(i = 0; i < size3; i++)
	{
		if(size1 == 16)
			temp = Hzk2[num][i];
		else if(size1 == 32)
			temp = Hzk3[num][i];
		else if(size1 == 64)
			temp = Hzk4[num][i];
		else return;

		for(m = 0; m < 8; m++)
		{
			if(temp & 0x01) OLED_DrawPoint(x, y, mode);
			else OLED_DrawPoint(x, y, !mode);
			temp >>= 1;
			y++;
		}
		x++;
		if((x - x0) == size1)
		{ x = x0; y0 = y0 + 8; }
		y = y0;
	}
}

//文字滚动显示
void OLED_ScrollDisplay(uint8_t num, uint8_t space, uint8_t mode)
{
	uint8_t i, n, t = 0, m = 0, r;

	while(1)
	{
		if(m == 0)
		{
			OLED_ShowChinese(72, 16, t, 16, mode);
			t++;
		}
		if(t >= num)
		{
			for(r = 0; r < 16 * space; r++)
			{
				for(i = 1; i < OLED_WIDTH; i++)
				{
					for(n = 0; n < OLED_PAGES; n++)
					{
						OLED_GRAM[i - 1][n] = OLED_GRAM[i][n];
					}
				}
				OLED_Refresh();
			}
			t = 0;
		}
		m++;
		if(m == 16) m = 0;

		for(i = 1; i < OLED_WIDTH; i++)
		{
			for(n = 0; n < OLED_PAGES; n++)
			{
				OLED_GRAM[i - 1][n] = OLED_GRAM[i][n];
			}
		}
		OLED_Refresh();
	}
}

/* 英文文本字符级滚动显示, 非阻塞。
 * 支持 2 条独立滚动行 (line=0: U1, line=1: U2)。
 *
 * 工作原理:
 * 1. 外部 (如 UART ISR) 填充缓冲: 左移旧字符, 新字符从右进
 * 2. 主循环调用 OLED_ScrollTextTick 显示缓冲最后 SCR_WIDTH 字符
 * 3. 效果: 文本从右向左滚动
 *
 * 用法:
 *   OLED_ScrollText(0, u1_buf, 32, 5, 1);   // 设置 U1 行 (y=5)
 *   OLED_ScrollText(1, u2_buf, 32, 17, 1);  // 设置 U2 行 (y=17)
 *   // 主循环:
 *   OLED_ScrollTextTick(0);  // 显示 U1
 *   OLED_ScrollTextTick(1);  // 显示 U2
 *   OLED_Refresh();
 */

#define SCR_WIDTH  14   /* 88px / 6px = 一屏字符数 */
#define SCR_LINES  2    /* 最多 2 条独立滚动行 */

typedef struct {
  char    *buf;     /* 外部缓冲指针 */
  uint8_t  len;     /* 缓冲有效长度 */
  uint8_t  pos;     /* 滚动位置 (0 ~ len-1) */
  uint8_t  y;       /* 行 y 坐标 */
  uint8_t  mode;    /* 显示模式: 0=正常, 1=反白 */
} ScrollLine;

static ScrollLine s_scroll[SCR_LINES] = {{0}};

void OLED_ScrollText(uint8_t line, char *buf, uint8_t len, uint8_t y, uint8_t mode)
{
	if(line >= SCR_LINES) return;
	s_scroll[line].buf  = buf;
	s_scroll[line].len  = len;
	s_scroll[line].pos  = 0;
	s_scroll[line].y    = y;
	s_scroll[line].mode = mode;
}

void OLED_ScrollTextTick(uint8_t line)
{
	if(line >= SCR_LINES || s_scroll[line].buf == 0 || s_scroll[line].len == 0) return;

	ScrollLine *s = &s_scroll[line];

	/* 关中断读缓冲, 防止 ISR 写入导致数据错乱 */
	__disable_irq();
	for(uint8_t i = 0; i < SCR_WIDTH; i++)
	{
		uint8_t idx = (s->pos + i) % s->len;
		char c = s->buf[idx];
		if(c < ' ' || c > '~') c = '.';
		OLED_ShowChar(i * 6, s->y, c, 8, s->mode);
	}
	__enable_irq();

	/* 推进滚动位置 */
	s->pos++;
	if(s->pos >= s->len) s->pos = 0;
}

//显示图片 (通过DrawPoint支持旋转)
void OLED_ShowPicture(uint8_t x, uint8_t y, uint8_t sizex, uint8_t sizey, uint8_t BMP[], uint8_t mode)
{
	uint16_t j = 0;
	uint8_t i, n, temp, m;
	uint8_t x0 = x, y0 = y;

	sizey = sizey / 8 + ((sizey % 8) ? 1 : 0);

	for(n = 0; n < sizey; n++)
	{
		for(i = 0; i < sizex; i++)
		{
			temp = BMP[j];
			j++;
			for(m = 0; m < 8; m++)
			{
				if(temp & 0x01) OLED_DrawPoint(x, y, mode);
				else OLED_DrawPoint(x, y, !mode);
				temp >>= 1;
				y++;
			}
			x++;
			if((x - x0) == sizex)
			{
				x = x0;
				y0 = y0 + 8;
			}
			y = y0;
		}
	}
}

//CH1115初始化 (0.5寸 88x48 OLED, I2C)
void OLED_Init(void)
{
	OLED_RES_Clr();
	HAL_Delay(200);
	OLED_RES_Set();
	HAL_Delay(200);

	// ===== CH1115 初始化命令序列 =====
	OLED_WR_Byte(0xAE, OLED_CMD);
	OLED_WR_Byte(0xD5, OLED_CMD);
	OLED_WR_Byte(0x50, OLED_CMD);
	OLED_WR_Byte(0xA8, OLED_CMD);
	OLED_WR_Byte(0x2F, OLED_CMD);
	OLED_WR_Byte(0xD3, OLED_CMD);
	OLED_WR_Byte(0x00, OLED_CMD);
	OLED_WR_Byte(0x40, OLED_CMD);
	OLED_WR_Byte(0x8D, OLED_CMD);
	OLED_WR_Byte(0x14, OLED_CMD);
	OLED_WR_Byte(0x20, OLED_CMD);
	OLED_WR_Byte(0x00, OLED_CMD);
	OLED_WR_Byte(0xA0, OLED_CMD);
	OLED_WR_Byte(0xC0, OLED_CMD);
	OLED_WR_Byte(0xDA, OLED_CMD);
	OLED_WR_Byte(0x02, OLED_CMD);
	OLED_WR_Byte(0x81, OLED_CMD);
	OLED_WR_Byte(0xFF, OLED_CMD);
	OLED_WR_Byte(0xD9, OLED_CMD);
	OLED_WR_Byte(0xF1, OLED_CMD);
	OLED_WR_Byte(0xDB, OLED_CMD);
	OLED_WR_Byte(0x30, OLED_CMD);
	OLED_WR_Byte(0xA6, OLED_CMD);

	x_low_offset = 0x00;
	x_high_offset = 0x00;

	OLED_Clear();

	HAL_Delay(100);
	OLED_WR_Byte(0xAF, OLED_CMD);
}


