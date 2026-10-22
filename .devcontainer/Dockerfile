FROM ubuntu:24.04

RUN apt-get update && \
    apt-get install -y --no-install-recommends \
        build-essential \
        bc \
        gcc-riscv64-linux-gnu \
        binutils-riscv64-linux-gnu \
        qemu-system-misc \
        gdb-multiarch \
        git \
        python3 \
        nano \
        vim \
        less \
        tree \
        file \
        curl \
        ca-certificates \
        && rm -rf /var/lib/apt/lists/*