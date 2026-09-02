#!/usr/bin/with-contenv bashio

echo "Start!"

if ls /dev/i2c-1; then
    echo "Found i2c access!";

    # display-cli passes the header message to the display through a file in
    # here. /run is a tmpfs, so the directory is gone on every container
    # start and has to be put back before the display looks for it.
    mkdir -p /run/uctronics-display

    echo "UCTRONICS OLED Display now be showing information";

    # exec, so the display replaces this shell and receives the SIGTERM that
    # stops the add-on directly. It clears the screen on the way out; behind
    # a shell the signal would go to bash and the panel would keep showing
    # whichever reading was up when the add-on stopped.
    exec /lcd_display/display
else
    echo "no found i2c!"
fi
