FROM opensuse/leap:15.4
RUN zypper refresh && zypper in -y pcre2-devel gcc-c++ clang cmake git ninja sudo rpm-build elfutils systemd-sysvinit python39
RUN useradd -m tux
RUN groupadd -g 2001 admin && usermod -G admin tux  && echo '%admin ALL=(ALL) NOPASSWD: ALL' >> /etc/sudoers


# normally `.` directory indicate Dockerfile directory, so COPY command is actually useless
# if run via `run_docker.ds` script, `.` directory indicate project root, so copy whole project files
COPY . /home/tux/dev
RUN chown -R tux:users /home/tux/dev && chmod -R 744 /home/tux/dev

USER tux

# under Github Actions, WORKDIR always indicates GITHUB_WORKSPACE (source code location)
# so, if create directory at WORKDIR, need root privilege
# (https://docs.github.com/en/actions/creating-actions/dockerfile-support-for-github-actions)
WORKDIR /home/tux/dev

# actual build dir is /home/tux/project_root_dir_for_cpack_rpm_build (for CPackRPM workaround)
CMD cat /proc/1/cgroup && \
    DIR="$(pwd)" && \
    mkdir /home/tux/project_root_dir_for_cpack_rpm_build && \
    cp -r ./ /home/tux/project_root_dir_for_cpack_rpm_build && \
    cd /home/tux/project_root_dir_for_cpack_rpm_build && \
    sh ./scripts/build_rpm.sh clang++ && \
    sudo mkdir "$DIR/build" && \
    sudo cp ./build-rpm/*.rpm "$DIR/build/" && \
    ls "$DIR/build"
