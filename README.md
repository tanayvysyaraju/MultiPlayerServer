# RPSD: Multiplayer Rock-Paper-Scissors Server

**RPSD** is a systems-level multiplayer Rock-Paper-Scissors server that pairs players via TCP sockets and manages real-time games using a custom protocol and UNIX process forking. Built in C for CS 214: Systems Programming at Rutgers, it simulates scalable networked gameplay with concurrent client handling.

---

## Project Motivation

Systems programmers often face:

* Concurrency and socket complexity in C
* Handling real-time client state
* Robust error and edge case management

**RPSD** tackles these by:

* Using process isolation for match handling
* Designing a state-aware multiplayer protocol
* Handling disconnects, duplicate logins, and rematches cleanly

---

## Features

* **TCP Socket Management**

  * Listens on a custom port via `bind()` and `listen()`
  * Accepts multiple clients via `accept()`

* **Custom Protocol (RPSP)**

  * Message types: `P` (Play), `W` (Wait), `B` (Begin), `M` (Move), `R` (Result), `C` (Continue), `Q` (Quit)
  * Enforces strict message formatting and parsing

* **Process Forking**

  * Each match runs in an isolated process via `fork()`
  * Parent handles matchmaking and child handles gameplay

* **Player State Tracking**

  * Prevents duplicate usernames across sessions
  * Detects disconnects and sends forfeits
  * Supports rematch logic and state cleanup

* **Robust Error Handling**

  * Invalid messages are dropped and logged
  * `SIGCHLD` used to reap zombie processes
  * Dead sockets removed from active/waiting lists

---

## Concurrency Architecture

RPSD uses **process-based concurrency**:

```bash
Main server (parent)
│
├── accept() → add to waiting queue
│
├── pair players → fork()
│   └── child handles individual match
│       └── RPSP messages processed in loop
│
└── SIGCHLD handler reaps finished games
```

---

## Test Plan

All features were verified through manual testing:

| Scenario           | Description                                        |   |           |   |                        |
| ------------------ | -------------------------------------------------- | - | --------- | - | ---------------------- |
| Win/Loss Match     | Correct result sent after one player beats another |   |           |   |                        |
| Draw Match         | Sends \`R                                          | D |           |   | \` on tie              |
| Disconnect Forfeit | Sends \`R                                          | F |           |   | \` to remaining player |
| Duplicate Login    | Duplicate name blocked with \`R                    | L | Logged in |   | \`                     |
| Concurrent Matches | Multiple forks allow isolated matches in parallel  |   |           |   |                        |

---

## Setup Instructions

### Prerequisites

* POSIX-compatible OS (macOS, Linux)
* `gcc` or any standard C compiler
* Terminal client (`telnet`, `nc`) for testing

### Build

```bash
make
```

### Run

```bash
./rpsd <port>
```

**Example:**

```bash
./rpsd 12345
```
