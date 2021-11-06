FROM i386/ubuntu:bionic
RUN apt update && apt install -y g++ sudo cmake git language-pack-ja ninja-build \
    automake libtool libpcre2-8-0 libpcre2-dev systemd-sysv
RUN useradd -m tux
RUN groupadd -g 2001 admin && usermod -G admin tux  && echo '%admin ALL=(ALL) NOPASSWD: ALL' >> /etc/sudoers

COPY . /home/tux/dev
RUN chown -R tux:users /home/tux/dev && chmod -R 744 /home/tux/dev

USER tux
WORKDIR /home/tux/dev
CMD cat /proc/1/cgroup && \
    sudo cp -rf ./ /home/tux/project && \
    sudo chown -R tux:users /home/tux/project && sudo chmod -R 744 /home/tux/project && \
    cd /home/tux/project && \
    sh tools/scripts/run_all_test.sh g++
