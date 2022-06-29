FROM opensuse/tumbleweed
RUN zypper refresh && zypper in -y pcre2-devel gcc-c++ clang cmake git ninja sudo systemd-sysvinit
RUN useradd -m tux
RUN groupadd -g 2001 admin && usermod -G admin tux  && echo '%admin ALL=(ALL) NOPASSWD: ALL' >> /etc/sudoers

COPY . /home/tux/dev
RUN chown -R tux:users /home/tux/dev && chmod -R 744 /home/tux/dev

USER tux
WORKDIR /home/tux/dev
CMD cat /proc/1/cgroup && sh tools/scripts/run_all_test.sh
