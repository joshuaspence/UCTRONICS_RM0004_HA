# UCTRONICS Display

Enables the the LCD display for UCTRONICS Pi 4 Rack Module.

This addon includes a splash screen that will show you the system information.

## First Step  - Enable i2c
### Option 1  - Official
[Official Documentation](https://www.home-assistant.io/common-tasks/os#enable-i2c-with-an-sd-card-reader) 

### Options 2 - Best Choise
This addon from Adam Outler, [GitHub adamoutler](https://github.com/adamoutler/HassOSConfigurator/tree/main/Pi4EnableI2C) to first enable the I2C interface, you will need to reboot twice as per his documentation. After I2C is enabled then you wil be able to use this. 

## Second Step - Enable this Addon.
1. Start the Addon
2. Check the "Log" and see if there are any errors.
3. Your OLED should be displaying.

## Show your own message

`display-cli` replaces the top line of the display with a message of your
choosing, while the CPU, RAM, temperature and disk readings carry on as
usual. The running display picks up a change within a couple of seconds;
there is nothing to restart.

It lives inside the add-on container, so it is run through `docker exec`
from a terminal on the Home Assistant host — the
[SSH & Web Terminal](https://github.com/hassio-addons/addon-ssh) add-on with
protection mode turned off is one way to get one. Find the container first,
because its name depends on how this add-on repository was added:

```bash
container=$(docker ps --format '{{.Names}}' | grep uc_tronics_display)

docker exec "$container" display-cli "Backup server"   # show a message
docker exec "$container" display-cli --show            # print what is set
docker exec "$container" display-cli --clear           # back to the IP address
```

The top line fits 19 characters. Longer messages, and anything outside
printable ASCII, are trimmed to what the display is able to draw.

The message is kept in `/run/uctronics-display/message` inside the
container. That is a tmpfs, so a message lasts until the add-on is
restarted and no longer, at which point the IP address comes back.

## Where the display code comes from

`lcd_display/` is a copy of
[joshuaspence/SKU_RM0004](https://github.com/joshuaspence/SKU_RM0004), a fork
of [UCTRONICS/SKU_RM0004](https://github.com/UCTRONICS/SKU_RM0004). The
directory layout matches the fork so that picking up later changes is a
straight copy of `project/`, `hardware/` and `Makefile`.
