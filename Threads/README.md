# Concurrent Voting Application — Client/Server in C
### Networking and Distributed Programming Assignment 2

---

## Project Overview

This project implements a **distributed voting system** in C with both:

1. **Concurrent Connectionless** — UDP-based servers (port 9000)
2. **Concurrent Connection-Oriented** — TCP-based servers (port 9001)

Each category is implemented three ways — **Processes**, **Threads**, and **Async I/O** — for a total of **6 server variants** and **2 shared clients**.

---

## File Structure

```
voting_app/
├── voting_protocol.h       — Shared wire protocol, data structures, utilities
│
├── udp_server_process.c    — [1] Concurrent Connectionless — PROCESSES (fork)
├── udp_server_thread.c     — [2] Concurrent Connectionless — THREADS (pthreads)
├── udp_server_async.c      — [3] Concurrent Connectionless — ASYNC I/O (select)
│
├── tcp_server_process.c    — [4] Concurrent Connection-Oriented — PROCESSES
├── tcp_server_thread.c     — [5] Concurrent Connection-Oriented — THREADS
├── tcp_server_async.c      — [6] Concurrent Connection-Oriented — ASYNC I/O
│
├── udp_client.c            — UDP client (works with all UDP servers)
├── tcp_client.c            — TCP client (works with all TCP servers)
│
├── Makefile
└── README.md
```

---

## Building

```bash
# Build everything
make all

# Build individual targets
make udp_server_process
make tcp_server_thread
make udp_client
# ... etc
```

**Dependencies:**
- `gcc` (C99 or later)
- `-lrt` (POSIX shared memory — for process-based servers)
- `-lpthread` (POSIX threads — for thread-based servers)
- Linux kernel ≥ 2.6 (for POSIX shared memory + pthreads)

---

## Architecture Summary

### Protocol (`voting_protocol.h`)
All 6 servers and 2 clients use a **common binary wire protocol** over UDP or TCP.

| Opcode | Direction | Description |
|--------|-----------|-------------|
| `0x01 MSG_VOTE_REQ`  | Client → Server | Cast a vote |
| `0x02 MSG_VOTE_ACK`  | Server → Client | Vote accepted |
| `0x03 MSG_VOTE_ERR`  | Server → Client | Vote rejected |
| `0x04 MSG_TALLY_REQ` | Client → Server | Request results |
| `0x05 MSG_TALLY_RESP`| Server → Client | Return tallies |
| `0x06 MSG_LIST_REQ`  | Client → Server | List candidates |
| `0x07 MSG_LIST_RESP` | Server → Client | Return list |
| `0xFF MSG_QUIT`      | Client → Server | End session (TCP) |

All messages are fixed-size packed structs, making parsing trivial.

---

### 1. Concurrent Connectionless — PROCESSES (`udp_server_process`)

```
Client A ──datagram──►  [Main Process]
Client B ──datagram──►     │  fork() ─► [Child A] — handles A
Client C ──datagram──►     │  fork() ─► [Child B] — handles B
                           │  fork() ─► [Child C] — handles C
```

- **Concurrency:** `fork()` per datagram
- **State sharing:** POSIX shared memory (`shm_open` / `mmap`)
- **Synchronisation:** POSIX semaphore (`sem_init` with `pshared=1`)
- **Zombie cleanup:** `SIGCHLD` → `waitpid(-1, WNOHANG)`

---

### 2. Concurrent Connectionless — THREADS (`udp_server_thread`)

```
Client A ──datagram──►  [Main Thread]
Client B ──datagram──►     │  pthread_create ─► [Thread A]
Client C ──datagram──►     │  pthread_create ─► [Thread B]
                           │  pthread_create ─► [Thread C]
```

- **Concurrency:** Detached `pthreads`
- **State sharing:** Global `VoteStore` (same address space)
- **Synchronisation:** `pthread_mutex_t`
- **Memory:** Each thread gets a heap-allocated `RequestCtx` (freed by thread)

---

### 3. Concurrent Connectionless — ASYNC I/O (`udp_server_async`)

```
Client A ──datagram──►  ┌──────────────────────────────────┐
Client B ──datagram──►  │   Event Loop  (select + NONBLOCK) │
Client C ──datagram──►  │   Work Queue  [A] [B] [C] ...     │
                        │   Process 1 item per cycle         │
                        └──────────────────────────────────┘
```

- **Concurrency:** Single-process, single-thread event loop
- **Mechanism:** `select()` on a non-blocking socket
- **Work queue:** Ring buffer of pending datagrams
- **Synchronisation:** None needed (no parallelism)

---

### 4. Concurrent Connection-Oriented — PROCESSES (`tcp_server_process`)

```
Client A ──connect──►  [Listening Process]
Client B ──connect──►     │  fork() ─► [Child A] ─── full session A
Client C ──connect──►     │  fork() ─► [Child B] ─── full session B
                          │  fork() ─► [Child C] ─── full session C
```

- Each child inherits the connected socket and handles the full TCP session
- Parent closes `connfd`; child closes `listenfd`
- Shared memory + semaphore for vote store (same as UDP process variant)

---

### 5. Concurrent Connection-Oriented — THREADS (`tcp_server_thread`)

```
Client A ──connect──►  [Main Thread]
Client B ──connect──►     │  pthread_create ─► [Thread A] ─── session A
Client C ──connect──►     │  pthread_create ─► [Thread B] ─── session B
```

- All threads share the same `VoteStore` via mutex
- Detached threads free their `ConnCtx` and close `connfd` on exit

---

### 6. Concurrent Connection-Oriented — ASYNC I/O (`tcp_server_async`)

```
                    ┌─────────────────────────────────────┐
Listener ──────────►│                                     │
Client A ──────────►│   select() multiplexes all fds      │
Client B ──────────►│   ClientState[] tracks per-fd state  │
Client C ──────────►│   Non-blocking reads; state machine  │
                    └─────────────────────────────────────┘
```

- Uses `select()` over the listen fd + all connected client fds
- `ClientState` struct tracks bytes-received for each partial message
- No blocking; partial messages are handled across multiple select cycles

---

## Running the Servers

### UDP Servers (port 9000)

```bash
# Option 1: Process-based
./udp_server_process
./udp_server_process 9000    # explicit port

# Option 2: Thread-based
./udp_server_thread

# Option 3: Async I/O
./udp_server_async
```

### TCP Servers (port 9001)

```bash
# Option 1: Process-based
./tcp_server_process

# Option 2: Thread-based
./tcp_server_thread

# Option 3: Async I/O
./tcp_server_async
```

---

## Running the Clients

### On a different machine (or same machine for testing):

```bash
# UDP client — connects to any UDP server
./udp_client 192.168.1.100           # port 9000 default
./udp_client 192.168.1.100 9000

# TCP client — connects to any TCP server
./tcp_client 192.168.1.100           # port 9001 default
./tcp_client 192.168.1.100 9001
```

For local testing (both on same machine):
```bash
./udp_client 127.0.0.1
./tcp_client 127.0.0.1
```

---

## Client Menu

Both clients present an interactive menu:

```
┌──────────────────────────────────┐
│   TCP VOTING CLIENT              │
├──────────────────────────────────┤
│  1. List Candidates              │
│  2. Cast My Vote                 │
│  3. View Current Tally           │
│  4. Quit                         │
└──────────────────────────────────┘
```

**Voting flow:**
1. Enter your name → assigned a random voter ID
2. Choose "List Candidates" to see options
3. Choose "Cast My Vote" → select candidate number
4. Server checks for duplicate votes (one vote per voter ID)
5. Choose "View Current Tally" to see live results

---

## Sample Tally Output

```
╔══════════════════════════════════════╗
║         VOTING RESULTS               ║
╠══════════════════════════════════════╣
║ Alice Johnson   [####----------------]  20% (4)
║ Bob Martinez    [##########----------]  50% (10)
║ Carol Nguyen    [######--------------]  30% (6)
║ David Osei      [--------------------]   0% (0)
║ Eva Petrov      [--------------------]   0% (0)
╠══════════════════════════════════════╣
║ Total votes : 20                      ║
║ Current lead: Bob Martinez            ║
╚══════════════════════════════════════╝
```

---

## Key Design Decisions

| Feature | Decision |
|---------|----------|
| Wire format | Fixed-size packed structs — no parsing needed |
| Duplicate vote detection | Voter ID tracked in `voter_ids[]` array |
| UDP reliability | Client-side `select()` timeout (5 seconds) |
| TCP framing | Fixed-size messages; `recv_all` loops until full |
| Async client state | Per-fd `ClientState` with `bytes_in/expected` |
| Process isolation | Each process has own stack/heap; SHM bridges them |
| Thread safety | `pthread_mutex` or POSIX semaphore on every write |

---

## Comparison of Concurrency Models

| | Processes | Threads | Async I/O |
|--|-----------|---------|-----------|
| Isolation | High (separate memory) | Low (shared) | N/A (single) |
| Overhead | High (fork cost) | Medium | Very Low |
| Scalability | Low-Medium | Medium-High | Very High |
| Complexity | Medium | Medium | High |
| Shared state | Via SHM + semaphore | Via mutex | No sync needed |
| Crash isolation | Yes | No | N/A |

---

*Assignment 2 — Networking and Distributed Programming*
