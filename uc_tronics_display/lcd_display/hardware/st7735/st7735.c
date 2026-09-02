/* vim: set ai et ts=4 sw=4: */
#include "st7735.h"
#include "time.h"
#include <stdio.h>
#include <string.h>
#include <sys/sysinfo.h>
#include <sys/vfs.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <net/if.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <fcntl.h>
#include "rpiInfo.h"
#include "message.h"

int i2cd = -1;

/* Pixels in the largest glyph any of the fonts provides, 16x26. */
#define GLYPH_MAX_PIXELS (16 * 26)

/* Burst helpers, defined with the rest of the i2c code further down. */
static void i2c_burst_begin(void);
static void i2c_burst_end(void);
static void i2c_burst_repeat(const uint8_t *pattern, uint32_t patternLength, uint32_t total);

/*
 * Set display coordinates
 */
void lcd_set_address_window(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1)
{
    // col address set
    i2c_write_command(X_COORDINATE_REG, x0 + ST7735_XSTART, x1 + ST7735_XSTART);
    // row address set
    i2c_write_command(Y_COORDINATE_REG, y0 + ST7735_YSTART, y1 + ST7735_YSTART);
    // write to RAM
    i2c_write_command(CHAR_DATA_REG, 0x00, 0x00);

    i2c_write_command(SYNC_REG, 0x00, 0x01);
}

/*
 * Display a single character
 *
 * The glyph is assembled in memory and sent as one burst, rather than a
 * separate three byte i2c transaction per pixel. The pixels are the same
 * either way; this is only how they reach the panel.
 *
 * The per-pixel form cost an 11x18 glyph 198 transactions, each carrying a
 * register byte the burst does not need, and each followed by a usleep(10)
 * that in practice takes nearer 80us because that is the granularity the
 * timer can offer.
 */
void lcd_write_char(uint16_t x, uint16_t y, char ch, FontDef font, uint16_t color, uint16_t bgcolor)
{
    /* The largest font is 16x26, at two bytes per pixel. */
    uint8_t buff[GLYPH_MAX_PIXELS * 2];
    uint32_t index = 0;
    uint32_t i, b, j;

    lcd_set_address_window(x, y, x + font.width - 1, y + font.height - 1);

    for (i = 0; i < font.height; i++)
    {
        b = font.data[(ch - 32) * font.height + i];
        for (j = 0; j < font.width; j++)
        {
            uint16_t pixel = ((b << j) & 0x8000) ? color : bgcolor;
            buff[index++] = (uint8_t)(pixel >> 8);
            buff[index++] = (uint8_t)(pixel & 0xFF);
        }
    }
    i2c_burst_transfer(buff, index);
}

void lcd_write_ch(uint16_t x, uint16_t y, char ch, FontType font, uint16_t color, uint16_t bgcolor)
{
    switch (font)
    {
    case FontType_7x10:
        lcd_write_char(x, y, ch, Font_7x10, color, bgcolor);
        break;
    case FontType_8x16:
        lcd_write_char(x, y, ch, Font_8x16, color, bgcolor);
        break;
    case FontType_11x18:
        lcd_write_char(x, y, ch, Font_11x18, color, bgcolor);
        break;
    case FontType_16x26:
        lcd_write_char(x, y, ch, Font_16x26, color, bgcolor);
        break;
    }
}

/*
 * display string
 */
void lcd_write_string(uint16_t x, uint16_t y, char *str, FontDef font, uint16_t color, uint16_t bgcolor)
{

    while (*str)
    {
        if (x + font.width >= ST7735_WIDTH)
        {
            x = 0;
            y += font.height;
            if (y + font.height >= ST7735_HEIGHT)
            {
                break;
            }

            if (*str == ' ')
            {
                // skip spaces in the beginning of the new line
                str++;
                continue;
            }
        }

        /* No sync here: lcd_write_char() ends its burst with one. */
        lcd_write_char(x, y, *str, font, color, bgcolor);
        x += font.width;
        str++;
    }
}

void lcd_write_str(uint16_t x, uint16_t y, char *str, FontType font, uint16_t color, uint16_t bgcolor)
{
    switch (font)
    {
    case FontType_7x10:
        lcd_write_string(x, y, str, Font_7x10, color, bgcolor);
        break;
    case FontType_8x16:
        lcd_write_string(x, y, str, Font_8x16, color, bgcolor);
        break;
    case FontType_11x18:
        lcd_write_string(x, y, str, Font_11x18, color, bgcolor);
        break;
    case FontType_16x26:
        lcd_write_string(x, y, str, Font_16x26, color, bgcolor);
        break;
    }
}

/*
 * fill rectangle
 */
void lcd_fill_rectangle(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
    uint8_t buff[BURST_MAX_LENGTH];
    uint16_t count = 0;
    // clipping
    if ((x >= ST7735_WIDTH) || (y >= ST7735_HEIGHT))
        return;
    if ((x + w - 1) >= ST7735_WIDTH)
        w = ST7735_WIDTH - x;
    if ((y + h - 1) >= ST7735_HEIGHT)
        h = ST7735_HEIGHT - y;
    lcd_set_address_window(x, y, x + w - 1, y + h - 1);

    /* A solid fill is the same two bytes over and over, so one buffer of
       the pattern serves the whole rectangle however large it is. */
    for (count = 0; count < BURST_MAX_LENGTH / 2; count++)
    {
        buff[count * 2] = color >> 8;
        buff[count * 2 + 1] = color & 0xFF;
    }

    /* One burst session for the whole rectangle rather than one per row.
       Re-entering burst mode each row cost three commands and a delay
       every time, which for a small rectangle came to more than the pixel
       data: a 6x10 bar segment took 44 writes to send 60 pixels. */
    i2c_burst_begin();
    i2c_burst_repeat(buff, sizeof(buff), (uint32_t)w * (uint32_t)h * 2);
    i2c_burst_end();
}

/*
 * fill screen
 */

void lcd_fill_screen(uint16_t color)
{
    lcd_fill_rectangle(0, 0, ST7735_WIDTH, ST7735_HEIGHT, color);
    i2c_write_command(SYNC_REG, 0x00, 0x01);
}

void lcd_draw_image(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t *data)
{
    uint16_t col = h - y;
    uint16_t row = w - x;
    lcd_set_address_window(x, y, x + w - 1, y + h - 1);
    i2c_burst_transfer(data, sizeof(uint16_t) * col * row);
}

uint8_t lcd_begin(void)
{
    // I2C Init
    i2cd = open("/dev/i2c-1", O_RDWR);
    if (i2cd < 0)
    {
        fprintf(stderr, "Device I2C-1 failed to initialize\n");
        return 1;
    }
    if (ioctl(i2cd, I2C_SLAVE_FORCE, I2C_ADDRESS) < 0)
    {
        fprintf(stderr, "Device I2C-1 failed to select address 0x%02X\n", I2C_ADDRESS);
        close(i2cd);
        i2cd = -1;
        return 1;
    }
    return 0;
}

/*
 * Release the i2c descriptor. Safe to call whether or not lcd_begin()
 * succeeded, and safe to call more than once.
 */
void lcd_end(void)
{
    if (i2cd >= 0)
    {
        close(i2cd);
        i2cd = -1;
    }
}

void i2c_write_data(uint8_t high, uint8_t low)
{
    uint8_t msg[3] = {WRITE_DATA_REG, high, low};
    write(i2cd, msg, 3);
    usleep(10);
}

void i2c_write_command(uint8_t command, uint8_t high, uint8_t low)
{
    uint8_t msg[3] = {command, high, low};
    write(i2cd, msg, 3);
    usleep(10);
}

/*
 * Burst mode, split so that several transfers can share one session.
 * Entering and leaving costs three commands and their delays, which for a
 * small transfer is more than the pixel data itself.
 */
static void i2c_burst_begin(void)
{
    i2c_write_command(BURST_WRITE_REG, 0x00, 0x01);
}

static void i2c_burst_end(void)
{
    i2c_write_command(BURST_WRITE_REG, 0x00, 0x00);
    i2c_write_command(SYNC_REG, 0x00, 0x01);
}

static void i2c_burst_chunks(const uint8_t *buff, uint32_t length)
{
    uint32_t count = 0;
    while (length > count)
    {
        uint32_t chunk = (length - count > BURST_MAX_LENGTH)
                             ? BURST_MAX_LENGTH
                             : (length - count);
        write(i2cd, buff + count, chunk);
        count += chunk;
        usleep(700);
    }
}

/*
 * Send `total` bytes by repeating a pattern buffer.
 *
 * Used for solid fills, where every chunk carries the same bytes. Both the
 * pattern length and the total are whole numbers of pixels, so repeating
 * the buffer never splits a pixel across two chunks.
 */
static void i2c_burst_repeat(const uint8_t *pattern, uint32_t patternLength, uint32_t total)
{
    while (total > 0)
    {
        uint32_t chunk = (total > patternLength) ? patternLength : total;
        write(i2cd, pattern, chunk);
        total -= chunk;
        usleep(700);
    }
}

void i2c_burst_transfer(uint8_t *buff, uint32_t length)
{
    i2c_burst_begin();
    i2c_burst_chunks(buff, length);
    i2c_burst_end();
}

/* The header line, drawn in the 8x16 font above the blue separator. */
#define HEADER_ROW_Y 0
/* The 8x16 font fits 19 characters across the 160 pixel display. Padding
   the header to that width lets one write cover the whole row, so it never
   has to be blanked first. */
#define HEADER_TEXT_COLUMNS 19
#define HEADER_TEXT_MAX (HEADER_TEXT_COLUMNS + 1)

/* Segments in the bar graph. */
#define BAR_SEGMENTS 10

/*
 * What the panel is currently showing.
 *
 * These let a redraw be skipped when it would change nothing, which is
 * most of the time. They are only true for as long as nobody paints over
 * the areas they describe, so lcd_display_layout() clears them: after the
 * screen has been filled, nothing is on it whatever these last recorded.
 */
static char headerDrawn[HEADER_TEXT_MAX] = {0};
static int headerPainted = 0;
static uint16_t metricDrawnX = 0;
static uint16_t metricDrawnWidth = 0;
static uint8_t barDrawnCount = 0;
static uint16_t barDrawnColor = 0;
static int barPainted = 0;

static void lcd_display_invalidate(void)
{
    headerPainted = 0;
    metricDrawnWidth = 0;
    barPainted = 0;
}

void lcd_display(uint8_t symbol)
{
    switch (symbol)
    {
    case 0:
        lcd_display_cpuLoad();
        break;
    case 1:
        lcd_display_ram();
        break;
    case 2:
        lcd_display_temp();
        break;
    case 3:
        lcd_display_disk();
        break;
    default:
        break;
    }
}

void lcd_display_percentage(uint8_t val, uint16_t color)
{
    uint8_t count = 0;
    uint8_t xCoordinate = 30;
    /* Round up, so any non-zero reading lights at least one segment, but
       leave the bar empty at exactly zero. Adding 10 before dividing, as
       this did previously, lit a segment at 0%. */
    uint8_t bars = (uint8_t)((val + 9) / 10);
    if (bars > BAR_SEGMENTS)
    {
        bars = BAR_SEGMENTS;
    }

    /* Repaint only the segments whose colour actually changes. Every
       segment is its own address window and burst, so redrawing all ten
       cost about a third of a screen change to alter a handful of them.
       The unlit tail is usually already grey and stays that way. */
    for (count = 0; count < BAR_SEGMENTS; count++)
    {
        uint16_t wanted = (count < bars) ? color : ST7735_GRAY;
        uint16_t shown = (count < barDrawnCount) ? barDrawnColor : ST7735_GRAY;

        if (!barPainted || wanted != shown)
        {
            lcd_fill_rectangle(xCoordinate, 60, 6, 10, wanted);
        }
        xCoordinate += 10;
    }
    barDrawnCount = bars;
    barDrawnColor = color;
    barPainted = 1;
}

/* The value field is sized for "100", the widest reading any metric
   produces, plus one character for the unit that follows it. */
#define METRIC_VALUE_DIGITS 3
#define METRIC_ROW_Y 35
#define METRIC_ROW_HEIGHT 20

/*
 * Draw one metric row, "LABEL:value unit", with its bar graph beneath.
 *
 * The row is built as a single string and written in one pass, rather than
 * blanked and then filled with three separate writes. lcd_write_char
 * paints the background colour for unset pixels, so the text erases what
 * it replaces as it goes; blanking first leaves the row empty for as long
 * as the text takes to draw, which at one i2c transaction per pixel shows
 * as a flicker on every screen change.
 *
 * Left-aligning the reading in a field of METRIC_VALUE_DIGITS is what puts
 * the unit at a fixed offset, so the row lands on exactly the pixels the
 * three separate writes used to land on.
 *
 * The width is still derived from the label rather than tabulated, which
 * is how the four per-metric functions arrived at their hand-written
 * constants: size the row for the label, three digits and a one-character
 * unit, then centre it.
 */
static void lcd_display_metric(char *label, uint8_t value, char *unit,
                               uint8_t barValue, uint16_t color)
{
    char row[24] = {0};
    uint16_t charWidth = Font_11x18.width;
    uint16_t rowWidth = 0;
    uint16_t labelX = 0;

    snprintf(row, sizeof(row), "%s%-*u%s", label, METRIC_VALUE_DIGITS, value, unit);
    rowWidth = (uint16_t)(strlen(row) * charWidth);
    if (rowWidth < ST7735_WIDTH)
    {
        labelX = (uint16_t)((ST7735_WIDTH - rowWidth) / 2);
    }

    /* Clear only what the previous row covered and this one will not.
       Clearing both margins outright meant repainting 1440 pixels of
       background on every screen change, black over black, which cost
       more of the change than the bar did. Nothing else draws in this
       row, so whatever the previous row did not cover is already
       background. Three of the four transitions in the rotation are
       between rows of equal width and now clear nothing at all. */
    if (metricDrawnWidth > 0)
    {
        uint16_t drawnEnd = (uint16_t)(metricDrawnX + metricDrawnWidth);
        uint16_t rowEnd = (uint16_t)(labelX + rowWidth);

        if (metricDrawnX < labelX)
        {
            lcd_fill_rectangle(metricDrawnX, METRIC_ROW_Y, (uint16_t)(labelX - metricDrawnX),
                               METRIC_ROW_HEIGHT, ST7735_BLACK);
        }
        if (drawnEnd > rowEnd)
        {
            lcd_fill_rectangle(rowEnd, METRIC_ROW_Y, (uint16_t)(drawnEnd - rowEnd),
                               METRIC_ROW_HEIGHT, ST7735_BLACK);
        }
    }
    metricDrawnX = labelX;
    metricDrawnWidth = rowWidth;
    lcd_write_string(labelX, METRIC_ROW_Y, row, Font_11x18, ST7735_WHITE, ST7735_BLACK);
    lcd_display_percentage(barValue, color);
}

/*
 * Draw the top line: the message set through display-cli if one is set,
 * and otherwise the address or fixed string that was shown before.
 *
 * Nothing else touches this row, so it persists across screen changes and
 * is only rewritten when its content actually differs.
 */
void lcd_display_header(void)
{
    char message[DISPLAY_MESSAGE_MAX] = {0};
    char iPSource[IP_ADDRESS_LENGTH] = {0};
    char text[HEADER_TEXT_MAX] = {0};
    char padded[HEADER_TEXT_MAX] = {0};

    if (display_message_read(message, sizeof(message)))
    {
        snprintf(text, sizeof(text), "%s", message);
    }
    else if (IP_SWITCH == IP_DISPLAY_OPEN)
    {
        get_ip_address_new(iPSource, sizeof(iPSource));                               // Get the IP address of the device's wireless network card
        snprintf(text, sizeof(text), "IP:%s", iPSource);
    }
    else
    {
        snprintf(text, sizeof(text), "%s", CUSTOM_DISPLAY);
    }

    /* Nothing to do when the line already says this. Cheap enough to call
       every refresh, which is what lets an address change be noticed
       rather than only a message change. */
    if (headerPainted && strcmp(text, headerDrawn) == 0)
    {
        return;
    }
    strcpy(headerDrawn, text);
    headerPainted = 1;

    /* Padded to the full width and written in one pass. lcd_write_char
       paints the background colour for unset pixels, so the text erases
       whatever it replaces as it goes. Blanking the row first would leave
       it empty for as long as the text takes to draw, and at one i2c
       transaction per pixel that gap is long enough to see. */
    snprintf(padded, sizeof(padded), "%-*s", HEADER_TEXT_COLUMNS, text);
    lcd_write_string(0, HEADER_ROW_Y, padded, Font_8x16, ST7735_WHITE, ST7735_BLACK);
}

/*
 * Paint the parts of the screen that never change: the background and the
 * separator under the header. Call once, before the first reading.
 *
 * This used to happen inside lcd_display_cpuLoad(), so every rotation back
 * to the CPU screen blanked the whole display and built it up again. That
 * cost 12800 pixels of fill plus a header repaint, and showed as the
 * screen going dark and refilling once every four screens.
 */
void lcd_display_layout(void)
{
    lcd_fill_screen(ST7735_BLACK);
    lcd_display_invalidate();
    lcd_fill_rectangle(0, 20, ST7735_WIDTH, 5, ST7735_BLUE);
    lcd_display_header();
}

void lcd_display_cpuLoad(void)
{
    uint8_t cpuLoad = get_cpu_message();

    lcd_display_metric("CPU:", cpuLoad, "%", cpuLoad, ST7735_GREEN);
}

void lcd_display_ram(void)
{
    float Totalram = 0.0;
    float freeram = 0.0;
    uint8_t residue = 0;

    get_cpu_memory(&Totalram, &freeram);
    /* get_cpu_memory() leaves the total at zero if /proc/meminfo cannot be
       read. Dividing by it yields NaN, and converting NaN to an integer is
       undefined behaviour rather than a harmless zero. */
    if (Totalram > 0.0)
    {
        residue = (Totalram - freeram) / Totalram * 100;
    }
    lcd_display_metric("RAM:", residue, "%", residue, ST7735_YELLOW);
}

void lcd_display_temp(void)
{
    uint8_t temp = get_temperature();
    uint8_t barValue = temp;

    /* The bar is a 0-100 scale, so a Fahrenheit reading drives it only
       after being converted back. Guarded because a sub-freezing reading
       would otherwise wrap round on an unsigned type. */
    if (TEMPERATURE_TYPE == FAHRENHEIT)
    {
        barValue = (temp > 32) ? (uint8_t)((temp - 32) / 1.8) : 0;
    }
    lcd_display_metric("TEMP:", temp, TEMPERATURE_TYPE == FAHRENHEIT ? "F" : "C",
                       barValue, ST7735_RED);
}

void lcd_display_disk(void)
{
    uint64_t totalBytes = 0;
    uint64_t usedBytes = 0;
    uint64_t availBytes = 0;
    uint64_t denominator = 0;
    uint8_t residue = 0;

    /* Totalled in bytes rather than whole GiB so that rounding each
       filesystem down no longer skews the percentage on small cards. */
    if (get_disk_usage(&totalBytes, &usedBytes, &availBytes) > 0)
    {
        /* df's Use%: used space over the space actually usable, rounded up
           the same way df rounds it, so the two agree. */
        denominator = usedBytes + availBytes;
        if (denominator > 0)
        {
            residue = (uint8_t)((usedBytes * 100 + denominator - 1) / denominator);
        }
    }
    lcd_display_metric("DISK:", residue, "%", residue, ST7735_BLUE);
}
