FROM emscripten/emsdk:3.1.64
RUN apt update && apt install -y sudo cmake git language-pack-ja ninja-build re2c
RUN useradd -m tux
RUN groupadd -g 2001 admin && usermod -G admin tux  && echo '%admin ALL=(ALL) NOPASSWD: ALL' >> /etc/sudoers

COPY . /home/tux/dev
RUN chown -R tux:users /home/tux/dev && chmod -R 744 /home/tux/dev

USER tux
WORKDIR /home/tux/dev

CMD DIR="$(pwd)" && sh ./scripts/em.sh && \
    sudo cp ./build-em/arsh.* /mnt/ && (sudo cp arsh* "$DIR" || true)