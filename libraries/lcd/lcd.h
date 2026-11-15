#define I2C_PORT i2c0
#define LCD_ADDR 0x27 
#define LCD_BACKLIGHT 0x08
#define LCD_BACKLIGHT_OFF 0x00
#define LCD_ENABLE_BIT 0x04

uint8_t lcd_backlight_state(bool state);
void lcd_i2c_send_byte(uint8_t data);
void lcd_toggle_enable(uint8_t data);
void lcd_send_cmd(uint8_t cmd);
void lcd_send_data(uint8_t cmd);
void lcd_clear(void);
void lcd_init(void);
void lcd_scroll_left(void);
void lcd_set_cursor(uint8_t row, uint8_t col);
void lcd_print(const char *str);