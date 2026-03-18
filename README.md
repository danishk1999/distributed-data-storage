# 🌐 Distributed Data Storage System
**Course:** CMPT 464 — Intro to Embedded Systems  
**Institution:** MacEwan University  
**Status:** ✅ Completed

A PicOS application for the CC1350 wireless microcontroller that implements a 
distributed data storage system where nodes can discover each other and remotely 
store, retrieve, and delete records over a wireless network.

## Features
- **Node Discovery** – finds all reachable neighbor nodes in the same group
- **Create Record** – stores a string record on a remote node
- **Delete Record** – deletes a record on a remote node by index
- **Retrieve Record** – fetches a record from a remote node by index
- **Local Storage** – view and reset records stored on the local node
- **Configurable** – group ID and node ID can be changed at runtime via UART

## Project Structure
├── app.cc          # Main application source
├── options.sys     # PicOS build configuration
└── README.md
## Hardware
- Texas Instruments CC1350 wireless microcontroller
- PicOS real-time operating system
- UART connection at 9600 baud for user interaction

## Message Protocol
| Type | Value | Description |
|------|-------|-------------|
| Discovery Request  | 0 | Broadcast to find neighbors |
| Discovery Response | 1 | Reply to discovery |
| Create Record      | 2 | Store a record on a remote node |
| Delete Record      | 3 | Delete a record on a remote node |
| Retrieve Record    | 4 | Fetch a record from a remote node |
| Response           | 5 | ACK/result for types 2–4 |

All packets use a 2-byte Network ID prefix (set to 0) and a 2-byte CRC suffix. 
Max packet size is 250 bytes.

## Node Configuration
Each node has:
- **Group ID** (2 bytes) – nodes with the same Group ID can communicate
- **Node ID** (1 byte) – unique ID between 1 and 25
- **Database** – up to 40 records, each up to 20 bytes, with owner ID and timestamp

## Usage
Connect to the node over UART at 9600 baud. The main menu will appear:

Group 1 Device #1 (0/40 records)
(G)roup ID
(N)ew device ID
(F)ind neighbors
(C)reate record on neighbor
(D)elete record on neighbor
(R)etrieve record from neighbor
(S)how local records
R(e)set local storage
Selection:

## Building
Open the project in your PicOS development environment and build targeting 
the CC1350. The `options.sys` file configures the radio driver.

## Authors
- Danish Kumar
