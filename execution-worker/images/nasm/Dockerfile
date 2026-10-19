# x86-64 assembly toolchain: NASM assembler + GNU linker (no libc).
FROM alpine:3.21
RUN apk add --no-cache nasm binutils \
 && nasm -v
WORKDIR /work
