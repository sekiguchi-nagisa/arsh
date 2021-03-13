FROM ubuntu:bionic
RUN apt update && apt install -y clang-4.0 llvm-4.0 sudo cmake git ninja-build libpcre3 libpcre3-dev systemd-sysv
RUN useradd -m tux
RUN groupadd -g 2001 admin && usermod -G admin tux  && echo '%admin ALL=(ALL) NOPASSWD: ALL' >> /etc/sudoers

COPY . /home/tux/dev
RUN chown -R tux:users /home/tux/dev && chmod -R 744 /home/tux/dev

USER tux
WORKDIR /home/tux/dev
CMD cat /proc/1/cgroup && \
    sh tools/scripts/run_all_test.sh clang++-4.0
