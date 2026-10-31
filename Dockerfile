# Build environment for KOReader on the PocketBook Pro 902 (ARMv6, soft-float, firmware 2.x)
FROM ubuntu:24.04
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        autoconf automake bison build-essential ca-certificates ccache cmake curl file flex \
        g++-multilib gcc-multilib gettext git libtool libtool-bin meson nasm ninja-build \
        p7zip-full patch perl pkg-config python3 texinfo unzip wget xz-utils zip \
        gawk gperf help2man libncurses-dev rsync bzip2 python3-dev cvs subversion \
    && rm -rf /var/lib/apt/lists/*
# glibc 2.9 (used by the toolchain recipe) breaks the docs step of the build with texinfo 7:
#  - texi2any's XS parser crashes on glibc's manual  -> makeinfo wrapper (pure-Perl parser, empty info file on failure)
#  - install-info does not like the old info file    -> no-op stub
# We do not need the documentation at all.
COPY makeinfo-wrapper.sh /usr/local/share/makeinfo-wrapper.sh
RUN rm -f /usr/bin/makeinfo && cp /usr/local/share/makeinfo-wrapper.sh /usr/bin/makeinfo && chmod 755 /usr/bin/makeinfo \
    && printf '#!/bin/sh\nexit 0\n' > /usr/local/bin/install-info && chmod 755 /usr/local/bin/install-info
ENV TEXINFO_XS_PARSER=0
# The legacy ARMv6 cross toolchain that the KOReader developers recommend for old devices (koxtoolchain "kindle")
RUN curl -fL -o /tmp/kindle.tar.gz https://github.com/koreader/koxtoolchain/releases/download/2025.05/kindle.tar.gz \
    && mkdir -p /opt && tar -xzf /tmp/kindle.tar.gz -C /opt && rm /tmp/kindle.tar.gz \
    && ls /opt/x-tools/arm-kindle-linux-gnueabi/bin/arm-kindle-linux-gnueabi-gcc
ENV PATH=/opt/x-tools/arm-kindle-linux-gnueabi/bin:$PATH
ENV CHOST=arm-kindle-linux-gnueabi
WORKDIR /work
