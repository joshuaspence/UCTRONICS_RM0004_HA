# Supervisor used to inject BUILD_FROM for add-ons without a build.yaml, and
# since 2026.04.0 it no longer does, which left this resolving to an empty
# base image name. Naming the base here fixes that; keeping it an ARG means a
# Supervisor that still passes one is honoured, and covers the architectures
# this image does not. ghcr.io/home-assistant/base is amd64 and arm64 only,
# so armhf, armv7 and i386 need their per-arch base passed in:
#   --build-arg BUILD_FROM=ghcr.io/home-assistant/armv7-base:latest
ARG BUILD_FROM=ghcr.io/home-assistant/base:latest
FROM ${BUILD_FROM}
ENV LANG=C.UTF-8

RUN set -xe \
    && apk add --no-cache gcc build-base git i2c-tools linux-headers cmake

# The whole tree, not lcd_display/*: a wildcard copies the contents of each
# directory it matches rather than the directory itself, which would flatten
# project/ and hardware/ into one directory and break the include paths.
COPY lcd_display/ /lcd_display/

# Built here rather than in run.sh. The sources are fixed once the image is
# built, so compiling on every start only delays the display, and a build
# failure surfaces while the add-on is installing instead of as a container
# that starts and then shows nothing.
RUN set -xe \
    && make -C /lcd_display \
    && ln -s /lcd_display/display-cli /usr/local/bin/display-cli

# Copy data for add-on
COPY run.sh /
RUN chmod a+x /run.sh

CMD [ "/run.sh" ]
