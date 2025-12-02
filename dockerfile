FROM ubuntu:22.04

# Устанавливаем всё необходимое
RUN apt-get update && apt-get install -y \
    build-essential \
    gcc \
    gdb \
    make \
    git \
    vim \
    htop \
    procps \
    strace \
    && rm -rf /var/lib/apt/lists/*

# Создаем рабочую директорию
WORKDIR /labs

# Копируем ВСЕ папки лабораторных из текущей директории
COPY lab1/ ./lab1/
COPY lab2/ ./lab2/

CMD ["/bin/bash"]