FROM opensuse/leap:15.4
RUN zypper refresh && zypper in -y pcre2-devel gcc-c++ clang cmake git ninja sudo rpm-build elfutils systemd-sysvinit
RUN useradd -m tux
RUN groupadd -g 2001 admin && usermod -G admin tux  && echo '%admin ALL=(ALL) NOPASSWD: ALL' >> /etc/sudoers

COPY . /home/tux/dev

USER tux
WORKDIR /home/tux/dev
CMD cat /proc/1/cgroup && \
    DIR="$(pwd)" && \
    PROJECT=/home/tux/project_root_dir_for_cpack_rpm_build && \
    sudo cp -rf ./ $PROJECT && \
    sudo chown -R tux:users $PROJECT && \
    sudo chmod -R 744 $PROJECT && \
    cd $PROJECT && \
    sh tools/scripts/build_rpm.sh clang++ && \
    sudo mkdir $DIR/build && \
    sudo cp build-rpm/*.rpm $DIR/build/ && \
    ls $DIR/build
