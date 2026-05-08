client:
	gcc -o evoting_client evoting_client.c client_stub.c evoting_common.h

server:
	gcc -o evoting_server evoting_server.c server_stub.c server_stub.h server_functions.c server_functions.h evoting_common.h

clean:
	rm -f evoting_server evoting_client *.o *.txt

all: clean server client