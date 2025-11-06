FROM gcc:12.2.0

# Устанавливаем strace и cmake
RUN apt-get update && apt-get install -y strace cmake

WORKDIR /app

COPY . .



