# RPC E-Voting Application

This project implements a simple E-Voting system using a custom "hand-rolled" Remote Procedure Call (RPC) framework in C. It bypasses complex libraries like gRPC or ONC RPC to demonstrate the fundamental principles of RPC: stubs, marshalling, network transmission, and dispatching.

## Project Structure

The application is divided into several layers to separate networking logic from business logic:

-   **`evoting_common.h`**: The source of truth for both client and server. It defines the procedure codes (e.g., `PROC_ADD_POSITION`), data structures, and shared helper functions (`send_line`, `recv_line`) for socket communication.
-   **`client_stub.c` / `.h`**: The client-side "proxies". These functions provide the same signature as the local server functions but handle the complexity of packaging and sending requests over the network.
-   **`server_stub.c` / `.h`**: The server-side dispatcher. It receives raw lines from the network, determines which procedure is being called, and routes the request to the correct handler.
-   **`server_functions.c` / `.h`**: The "meat" of the application. These handlers implement the actual logic (e.g., writing to files) without needing to know they are being called over a network.
-   **`evoting_client.c`**: The interactive CLI for voters and administrators.
-   **`evoting_server.c`**: The daemon that listens for incoming TCP connections and manages the request loop.

## RPC Lifecycle: How a Request Works

Every remote call follows a strict lifecycle. Below is the trace for the `stub_add_position` function:

### 1. Initiation (Client Application)
The client application (`evoting_client.c`) decides to add a new voting position. It calls the stub as if it were a local function:
```c
int result = stub_add_position("President");
```

### 2. Marshalling (Client Stub)
Inside `client_stub.c`, the `stub_add_position` function "marshals" (packages) the arguments into a wire-friendly format—in this case, a pipe-delimited string:
```c
snprintf(msg, sizeof(msg), "4|%s\n", position); // Results in "4|President\n"
```
*Note: `4` is the procedure code for `PROC_ADD_POSITION` defined in `evoting_common.h`.*

### 3. Transmission (Client -> Server)
The stub calls `send_line(g_sock, msg)` which writes the bytes over the TCP socket to the server.

### 4. Unmarshalling & Dispatch (Server Stub)
The server (`evoting_server.c`) accepts the connection and reads the line. It passes the line to `server_dispatch` in `server_stub.c`:
1.  It extracts the first field (`4`) using `strtok_r`.
2.  It identifies the request as `PROC_ADD_POSITION`.
3.  It extracts the remaining arguments (`"President"`).

### 5. Method Invocation (Server Logic)
The dispatcher calls the actual service implementation:
```c
handle_add_position(cfd, "President");
```
In `server_functions.c`, this function appends the position to `positions.txt` and prepares a success status.

### 6. Marshalling Result & Transmission (Server -> Client)
The server handler marshals the result (e.g., `"1\n"` for success) and sends it back using `send_line(cfd, "1\n")`.

### 7. Unmarshalling Result & Return (Client Stub)
Back in `client_stub.c`, the `stub_add_position` function is waiting on `recv_line`. It receives `"1"`, converts it to an integer using `atoi()`, and returns `1` to the main application.

## Wire Protocol

The protocol is text-based and newline-terminated for simplicity:
-   **Request Format**: `PROC_CODE|ARG1|ARG2|...`
-   **Simple Response**: `STATUS_CODE` (usually `0` or `1`)
-   **List Response**: Multiple lines followed by an `END` sentinel.

## Building and Running

### Prerequisites
- GCC compiler
- Linux/Unix environment (uses POSIX sockets)

### Compilation
Use the provided `Makefile`:
```bash
make all
```

### Running the Server
Start the server first to listen for connections:
```bash
./evoting_server
```

### Running the Client
In a separate terminal, start the client:
```bash
./evoting_client <server_ip>
```

## Data Persistence
The server maintains state in simple text files:
- `admin.txt`: Admin credentials.
- `voters.txt`: Voter credentials and voting status.
- `positions.txt`: Available positions.
- `contestants.txt`: Contestants and their vote counts.
