# ─────────────────────────────────────────────────────────────────
# Makefile — Concurrent Voting Application (Client-Server in C)
# ─────────────────────────────────────────────────────────────────

CC      = gcc
CFLAGS  = -Wall -Wextra -O2 -I.
LIBS_RT = -lrt
LIBS_PT = -lpthread
LIBS_BOTH = -lpthread -lrt

# ── all targets ──────────────────────────────────────────────────
.PHONY: all clean

all: \
    udp_server_process \
    udp_server_thread  \
    udp_server_async   \
    tcp_server_process \
    tcp_server_thread  \
    tcp_server_async   \
    udp_client         \
    tcp_client

# ── UDP Servers ──────────────────────────────────────────────────
udp_server_process: udp_server_process.c voting_protocol.h
	$(CC) $(CFLAGS) -o $@ $< $(LIBS_RT)

udp_server_thread: udp_server_thread.c voting_protocol.h
	$(CC) $(CFLAGS) -o $@ $< $(LIBS_PT)

udp_server_async: udp_server_async.c voting_protocol.h
	$(CC) $(CFLAGS) -o $@ $<

# ── TCP Servers ──────────────────────────────────────────────────
tcp_server_process: tcp_server_process.c voting_protocol.h
	$(CC) $(CFLAGS) -o $@ $< $(LIBS_RT)

tcp_server_thread: tcp_server_thread.c voting_protocol.h
	$(CC) $(CFLAGS) -o $@ $< $(LIBS_PT)

tcp_server_async: tcp_server_async.c voting_protocol.h
	$(CC) $(CFLAGS) -o $@ $<

# ── Clients ──────────────────────────────────────────────────────
udp_client: udp_client.c voting_protocol.h
	$(CC) $(CFLAGS) -o $@ $<

tcp_client: tcp_client.c voting_protocol.h
	$(CC) $(CFLAGS) -o $@ $<

# ── clean ─────────────────────────────────────────────────────────
clean:
	rm -f udp_server_process udp_server_thread udp_server_async \
	      tcp_server_process tcp_server_thread tcp_server_async \
	      udp_client tcp_client
