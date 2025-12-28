FROM opensuse/tumbleweed

RUN zypper refresh && zypper in -y sudo systemd-sysvinit glibc-locale shadow cmake

RUN useradd -m tux
RUN groupadd -g 2001 admin && usermod -G admin tux  && echo '%admin ALL=(ALL) NOPASSWD: ALL' >> /etc/sudoers

COPY . /home/tux/dev
RUN chown -R tux:users /home/tux/dev && chmod -R 744 /home/tux/dev

RUN zypper addrepo --no-gpgcheck -f https://download.opensuse.org/repositories/home:nsekiguchi/openSUSE_Tumbleweed/home:nsekiguchi.repo && \
    zypper refresh && \
    zypper install -y arsh

USER tux
WORKDIR /home/tux/dev
CMD cat /proc/1/cgroup && ./scripts/check_package.arsh
