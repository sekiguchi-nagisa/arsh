FROM ubuntu:24.04

RUN useradd -m tux
RUN groupadd -g 2001 admin && usermod -G admin tux  && echo '%admin ALL=(ALL) NOPASSWD: ALL' >> /etc/sudoers

COPY . /home/tux/dev
RUN chown -R tux:users /home/tux/dev && chmod -R 744 /home/tux/dev

RUN apt update && apt install -y curl gpg git

RUN echo 'deb http://download.opensuse.org/repositories/home:/nsekiguchi/xUbuntu_24.04/ /' | tee /etc/apt/sources.list.d/home:nsekiguchi.list && \
    curl -fsSL https://download.opensuse.org/repositories/home:nsekiguchi/xUbuntu_24.04/Release.key | gpg --dearmor | tee /etc/apt/trusted.gpg.d/home_nsekiguchi.gpg > /dev/null && \
    apt update && \
    apt install -y arsh

USER tux
WORKDIR /home/tux/dev
CMD cat /proc/1/cgroup && ./scripts/check_package.arsh
