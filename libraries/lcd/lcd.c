#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "lcd.h"
#include <string.h>

/**
 * @brief Convert a boolean backlight state to the corresponding LCD value.
 *
 * Maps a logical backlight state to the byte constant used by the LCD driver.
 * No I/O or hardware access is performed by this function; it only returns
 * the appropriate constant.
 *
 * @param state Boolean desired backlight state:
 *              - true  => backlight on
 *              - false => backlight off
 * @return uint8_t The value to use for the LCD backlight:
 *                 - LCD_BACKLIGHT when state is true
 *                 - LCD_BACKLIGHT_OFF when state is false
 */
uint8_t lcd_backlight_state(bool state)
{
    return state ? LCD_BACKLIGHT : LCD_BACKLIGHT_OFF;
};

/**
 * @brief Send a single byte to the LCD over I2C.
 *
 * Performs a blocking I2C write of a single byte to the LCD device at LCD_ADDR
 * using I2C_PORT, then waits 50 microseconds to allow the LCD to process the
 * received byte.
 *
 * @param data The byte to transmit. This may represent a command or display
 *             data depending on the surrounding driver state (e.g. RS/EN bits).
 *
 * @note The underlying i2c_write_blocking return value is not checked; I2C
 *       transmission errors are ignored by this function.
 *
 * @note This function blocks (both for the I2C transfer and the 50µs delay).
 *       Do not call from time-critical interrupt context or without
 *       serializing access to the shared I2C bus.
 *
 * @pre I2C_PORT and LCD_ADDR must be properly initialized and configured.
 * @return void
 */
void lcd_i2c_send_byte(uint8_t data) 
{
    i2c_write_blocking(I2C_PORT, LCD_ADDR, &data, 1, false);
    sleep_us(50);
}

/**
 * @brief Pulse the LCD enable line to latch data/commands.
 *
 * Sends the provided byte with LCD_ENABLE_BIT asserted, waits 500 microseconds,
 * then sends the byte with LCD_ENABLE_BIT cleared and waits another 500 microseconds.
 * This produces a single enable pulse suitable for latching data or commands
 * into an HD44780-compatible LCD via an I2C expander/backpack.
 *
 * @param data Byte containing the control/data bits to send. The function will
 *             temporarily set and clear LCD_ENABLE_BIT in this byte; other
 *             control bits (e.g., RS/RW) should already be encoded by the caller.
 *
 * @note Calls lcd_i2c_send_byte() and sleep_us(), which must be available in
 *       the environment. The function is blocking and introduces ~1 ms of delay.
 * @note Not inherently reentrant or thread-safe if the underlying I2C/send
 *       routines are not.
 * @pre The LCD must be initialized and ready to receive commands/data.
 * @return void
 */
void lcd_toggle_enable(uint8_t data) 
{
    lcd_i2c_send_byte(data | LCD_ENABLE_BIT);
    sleep_us(500);
    lcd_i2c_send_byte(data & ~LCD_ENABLE_BIT);
    sleep_us(500);
}

/**
 * @brief Write a 4-bit nibble to the LCD via the I2C expander and toggle the Enable line.
 *
 * This function composes a control byte from the LCD_BACKLIGHT state and the RS bit
 * (determined by is_data), ORs it with the provided nibble, and sends a three-step
 * sequence to the I2C expander:
 *   1. Send the data/control byte with Enable low.
 *   2. Pulse Enable high by sending the same byte with LCD_ENABLE_BIT set.
 *   3. Wait for the device to register the pulse (sleep_us(50), datasheet minimum 37 µs).
 *   4. Clear the Enable bit by sending the data/control byte with LCD_ENABLE_BIT cleared.
 *
 * The function uses lcd_i2c_send_byte() for each I2C transmission and preserves the
 * LCD_BACKLIGHT bit in all writes.
 *
 * Parameters:
 *   nibble   - 4-bit value to be written to the LCD data pins; it should already be
 *              positioned/masked to match the expander pins used for the LCD (i.e.,
 *              the caller is responsible for any required shifting or bit placement).
 *   is_data  - when true, sets the RS bit so the nibble is treated as data; when false,
 *              the nibble is treated as a command.
 *
 * Returns:
 *   None. Any errors from lcd_i2c_send_byte() are not propagated by this function.
 *
 * Notes:
 *   - Timing: the function uses a 50 µs delay to satisfy the LCD timing requirement
 *     (datasheet minimum specified as 37 µs).
 *   - Concurrency: this function does not perform locking; callers should ensure
 *     exclusive access to the bus/LCD if required.
 */
static void lcd_write_nibble(bool state, uint8_t nibble, bool is_data) 
{
    uint8_t control = lcd_backlight_state(state) | (is_data ? 0x01 : 0x00);
    uint8_t data = nibble | control;

    lcd_i2c_send_byte(data);
    lcd_i2c_send_byte(data | LCD_ENABLE_BIT);
    sleep_us(50); // min 37 us wg datasheet
    lcd_i2c_send_byte(data & ~LCD_ENABLE_BIT);
}

/**
 * @brief Send a single byte of display data to the LCD (4-bit mode).
 *
 * This function transmits an 8-bit value to the LCD by splitting it into two
 * 4-bit nibbles and sending them sequentially:
 *   - first the high nibble (bits 7..4),
 *   - then the low nibble  (bits 3..0).
 *
 * Both nibble writes are performed with RS = true, so the LCD treats the
 * transmitted value as data (character) rather than an instruction/command.
 * After the two nibble writes, the function waits approximately 50 microseconds
 * to allow the LCD controller time to process the received byte.
 *
 * @param data 8-bit value to send to the LCD data register (typically a
 *             character code).
 *
 * @pre The LCD must already be initialized and configured for 4-bit operation.
 * @pre The helper lcd_write_nibble() is expected to accept a nibble located in
 *      the upper 4 bits of the byte passed and to perform the required
 *      strobing/timing for a nibble write.
 *
 * @post The specified data byte will have been presented to the LCD and the
 *       function will have delayed long enough for basic processing (no busy
 *       flag polling is performed).
 *
 * @warning This function blocks for the specified delay and does not return an
 *          error code. If multiple execution contexts access the LCD hardware,
 *          serialize access (e.g., with a mutex) to avoid bus contention.
 */
void lcd_send_cmd(uint8_t cmd) 
{
    lcd_write_nibble(true, cmd & 0xF0, false);
    lcd_write_nibble(true, (cmd << 4) & 0xF0, false);
    sleep_us(50);
}

/**
 * lcd_send_data
 *
 * Send a single 8-bit data value to the LCD using two 4-bit transfers.
 *
 * The function transmits the high nibble first, then the low nibble, by calling
 * lcd_write_nibble(...) twice. After both nibbles are sent it waits ~50
 * microseconds to give the LCD time to process the received data.
 *
 * Parameters:
 *   cmd  - 8-bit data byte to be written to the display (character or data
 *          register value).
 *
 * Returns:
 *   void
 *
 * Side effects and notes:
 *   - Assumes the LCD and supporting interface are already initialized and
 *     configured for 4-bit mode.
 *   - Relies on lcd_write_nibble(...) to perform the actual bus/expander writes.
 *   - The second parameter passed to lcd_write_nibble(...) is false in this
 *     implementation to indicate the transfer type used by this module.
 *   - The function is blocking and includes an explicit sleep_us(50) delay;
 *     callers should account for this timing in time-sensitive contexts.
 *   - If used from multiple execution contexts, callers must ensure proper
 *     synchronization around the shared bus/expander access.
 */
void lcd_send_data(uint8_t cmd) 
{
    lcd_write_nibble(true, cmd & 0xF0, true);
    lcd_write_nibble(true, (cmd << 4) & 0xF0, true);
    sleep_us(50);
}

/**
 * @brief Clear the LCD display.
 *
 * Sends the controller "clear display" command (0x01) using lcd_send_cmd()
 * and waits for the required command execution time by sleeping for ~2 ms.
 *
 * Preconditions:
 *  - The LCD hardware and any underlying bus (e.g., I2C) must be initialized
 *    before calling this function.
 *
 * Side effects:
 *  - Clears all characters on the display.
 *  - Resets the cursor position to the home position (controller-dependent).
 *  - Blocks the caller for the duration of the delay.
 *
 * Thread safety:
 *  - Not inherently thread-safe; ensure synchronization if lcd_send_cmd() is
 *    used concurrently from multiple contexts.
 *
 * Parameters: None
 * Returns: void
 */
void lcd_clear(void) 
{
    lcd_send_cmd(0x01);
    sleep_ms(2);
}

/**
 * Initialize the character LCD in 4-bit mode.
 *
 * Performs the required power-on and mode setup sequence for a Hitachi-compatible
 * character LCD when driven via an I2C expander or similar interface.
 *
 * Behavior:
 *  - Waits 50 ms for LCD power-up/stabilization.
 *  - Sends initialization commands (via lcd_send_cmd):
 *      0x33 - reset/initialization sequence
 *      0x32 - set 4-bit interface
 *      0x28 - function set: 4-bit, 2-line, 5x8 dots
 *      0x0C - display on, cursor off, blink off
 *      0x06 - entry mode: increment cursor, no display shift
 *  - Clears the display by calling lcd_clear().
 *
 * Notes and assumptions:
 *  - Depends on implementations of lcd_send_cmd(uint8_t) and lcd_clear(void),
 *    which must handle lower-level timing and I/O (e.g., I2C expander toggles).
 *  - This function blocks for the delays required by the LCD; it is not
 *    asynchronous.
 *  - Intended to be called once after power-up or reset before using the LCD.
 */
void lcd_init(void) 
{
    sleep_ms(50);
    lcd_send_cmd(0x33);
    lcd_send_cmd(0x32);
    lcd_send_cmd(0x28);
    lcd_send_cmd(0x0C);
    lcd_send_cmd(0x06);
    lcd_clear();
}

/**
 * Set the LCD cursor to the specified row and column.
 *
 * Computes the DDRAM address using fixed row offsets (0x00 for row 0,
 * 0x40 for row 1), composes the "Set DDRAM Address" command (0x80 | addr),
 * and sends it via lcd_send_cmd().
 *
 * @param row Zero-based row index. Valid values: 0..1. Behavior is undefined for row >= 2.
 * @param col Zero-based column index. Typical modules use 0..15 for a 16x2 display;
 *            behavior is undefined for out-of-range column values (no bounds checking).
 *
 * @note This function assumes a common two-line HD44780-compatible address map.
 * @note Calling this will issue a command to the display (through lcd_send_cmd()),
 *       which may block while the bus/driver processes the instruction.
 * @pre The LCD must be initialized and ready to receive commands.
 * @return void
 */
void lcd_set_cursor(uint8_t row, uint8_t col) 
{
    uint8_t offsets[] = {0x00, 0x40};
    lcd_send_cmd(0x80 | (offsets[row] + col));
}

void lcd_scroll_left(void) 
{
    lcd_send_cmd(0x18);
}

/**
 * @brief Print a null-terminated string to the LCD via the I2C interface.
 *
 * Sends each character of the provided C string to the LCD by repeatedly
 * calling lcd_send_data() until the terminating NUL ('\0') is reached.
 *
 * @param str Pointer to a null-terminated string to be displayed on the LCD.
 *
 * @note The function does not insert line breaks, perform wrapping, or pad
 *       output; characters are sent exactly as they appear in the input string.
 * @note The caller is responsible for ensuring the string is null-terminated
 *       and remains valid for the duration of the call.
 *
 * @warning Passing a NULL pointer or a non-null-terminated buffer results in
 *          undefined behavior.
 *
 * @see lcd_send_data
 * @pre The LCD must be initialized and ready to receive data.
 * @return void
**/
void lcd_print(const char *str) 
{
    while (*str) {
        lcd_send_data(*str++);
    }
}