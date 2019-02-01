FROM opensuse/tumbleweed
RUN zypper in -y pcre-devel gcc-c++ clang cmake git ninja autoconf automake hostname sudo
RUN useradd -m tux
RUN groupadd -g 2001 admin && usermod -G admin tux  && echo '%admin ALL=(ALL) NOPASSWD: ALL' >> /etc/sudoers

COPY . /home/tux/dev
RUN chown -R tux:users /home/tux/dev && chmod -R 744 /home/tux/dev

USER tux
WORKDIR /home/tux/dev
CMD tools/scripts/setup.sh && cd build && ninja && ctest --output-on-failure
