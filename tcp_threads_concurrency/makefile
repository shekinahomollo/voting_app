# Commands made for linux or WSL terminal

# Compiles the server executable
# make server
server:
	gcc server.c -o server

# Compiles the client executable
# make client
client:
	gcc client.c -o client 

# Deletes the client and server executables. Use when recompiling to get fresh executables
# make clean
clean:
	rm -f client server

# Performs the whole process of cleaning and compilation of files and clears the terminal
# make whole
whole:
	make clean ; make server ; make client ; clear