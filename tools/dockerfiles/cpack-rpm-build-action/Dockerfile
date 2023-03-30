FROM opensuse/leap:15.4
RUN zypper refresh && zypper in -y pcre2-devel gcc-c++ clang cmake git ninja sudo rpm-build elfutils systemd-sysvinit
RUN useradd -m tux
RUN groupadd -g 2001 admin && usermod -G admin tux  && echo '%admin ALL=(ALL) NOPASSWD: ALL' >> /etc/sudoers

# normally, run under Github Actions. must be run root and does not set WORKDIR
# see https://docs.github.com/en/actions/creating-actions/dockerfile-support-for-github-actions

COPY . /home/tux/dev
RUN chown -R tux:users /home/tux/dev && chmod -R 744 /home/tux/dev

# actual build dir is /home/tux/project_root_dir_for_cpack_rpm_build (for CPackRPM workaround)
USER tux
WORKDIR /home/tux/dev
CMD cat /proc/1/cgroup && \
    DIR="$(pwd)" && \
    mkdir /home/tux/project_root_dir_for_cpack_rpm_build && \
    cp -r ./ /home/tux/project_root_dir_for_cpack_rpm_build && \
    cd /home/tux/project_root_dir_for_cpack_rpm_build && \
    sh ./tools/scripts/build_rpm.sh clang++ && \
    mkdir $DIR/build && \
    cp ./build-rpm/*.rpm $DIR/build/ && \
    ls $DIR/build
