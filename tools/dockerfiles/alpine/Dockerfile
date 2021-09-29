FROM alpine:latest
RUN apk update && apk add gcc g++ cmake ninja pcre2 pcre2-dev sudo git

COPY . /root/dev

WORKDIR /root/dev
CMD cat /proc/1/cgroup && \
    sh tools/scripts/run_all_test.sh g++
