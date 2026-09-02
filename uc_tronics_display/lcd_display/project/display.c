/******
Demo for ssd1306 i2c driver for  Raspberry Pi
******/
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include "st7735.h"
#include "time.h"
#include <unistd.h>

/* Screens lcd_display() cycles through, and how long each one stays up. */
#define SCREEN_COUNT 4
#define SCREEN_DWELL_SECONDS 2

static volatile sig_atomic_t running = 1;

static void request_stop(int signum)
{
	(void)signum;
	running = 0;
}

int main(void)
{
	uint8_t symbol = 0;
	struct sigaction action;

	if(lcd_begin())      //LCD Screen initialization
	{
		return 1;
	}

	/* systemd stops this service with SIGTERM. Without a handler the
	   process dies mid-frame, leaving a stale reading on a display nothing
	   is updating any more, and the i2c descriptor open. Catching it lets
	   the screen be cleared on the way out.

	   sa_flags stays zero, so SA_RESTART is off and the sleep below is
	   interrupted by delivery. A stop therefore takes effect at once
	   rather than after the current screen finishes its dwell. */
	memset(&action, 0, sizeof(action));
	action.sa_handler = request_stop;
	sigaction(SIGTERM, &action, NULL);
	sigaction(SIGINT, &action, NULL);

	/* The background and separator are drawn once. Nothing repaints them,
	   so the screen never goes dark between readings. */
	lcd_display_layout();

	sleep(1);
	while(running)
	{
		/* Rewrites itself only when the message or the address changed,
		   so this costs nothing on a typical pass. */
		lcd_display_header();
		lcd_display(symbol);

		sleep(SCREEN_DWELL_SECONDS);
		if(!running)
		{
			break;
		}

		symbol = (uint8_t)((symbol + 1) % SCREEN_COUNT);
	}

	lcd_fill_screen(ST7735_BLACK);
	lcd_end();
	return 0;
}
