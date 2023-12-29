FROM alpine:latest
RUN apk update && apk add gcc g++ cmake ninja pcre2 pcre2-dev sudo git python3

COPY . /root/dev

WORKDIR /root/dev
CMD cat /proc/1/cgroup && \
    LDFLAGS='-Wl,-z,stack-size=1024768' HOME=/root sh scripts/run_all_test.sh g++
