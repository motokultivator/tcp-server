all: example_echo_server example_echo_client

example_echo_client: example_echo_client.c afewmacros.h
	gcc example_echo_client.c -lpthread -o example_echo_client

example_echo_server: example_echo_server.c defs.h server.o context.o afewmacros.h config.h
	gcc example_echo_server.c server.o context.o -lpthread -o example_echo_server

example_echo_server_aarch64: example_echo_server.c defs.h server.c context_aarch64.o afewmacros.h config.h
	aarch64-linux-gnu-gcc example_echo_server.c server.c context_aarch64.o -lpthread -o example_echo_server_aarch64

server.o: server.c defs.h config.h
	gcc server.c -c -o server.o

context.o: scheduler/run_x86_64.asm
	nasm -f elf64 scheduler/run_x86_64.asm -o context.o

context_aarch64.o: scheduler/run_aarch64.S
	aarch64-linux-gnu-as scheduler/run_aarch64.S -o context_aarch64.o

clean:
	rm -rf server.o context.o context_aarch64.o example_echo_client example_echo_server example_echo_server_aarch64
