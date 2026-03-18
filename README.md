# 🌐 Distributed Data Storage System
**Course:** CMPT 464 — Intro to Embedded Systems  
**Institution:** MacEwan University  
**Status:** ✅ Completed

## Overview
Designed and implemented a distributed data storage system where multiple 
nodes can communicate, store, retrieve, and delete information records across 
the network. Each node operates independently while being part of a larger 
connected node group.

## Features
- Discover reachable neighbor nodes within the node group
- Store information records on remote nodes
- Retrieve information records stored on other nodes
- Delete information records stored on other nodes
- Accept and store information records received from other nodes
- View locally stored information records
- Delete ALL locally stored information records

## System Architecture

Node A ←──→ Node B
↕              ↕
Node C ←──→ Node D
Each node can:

Discover neighbors
Store / Retrieve / Delete records
Accept incoming records from other nodes

## Technologies Used
- **C / C++** — Core implementation language
- **Socket Programming** — Node-to-node communication
- **Distributed Systems Concepts** — Node discovery, replication, CRUD
- **File I/O** — Local record storage and management

## Core Operations
| Operation | Description |
|---|---|
| Discover Nodes | Find all reachable neighbor nodes in the group |
| Store Remote | Save a record on another node |
| Retrieve Remote | Fetch a record stored on another node |
| Delete Remote | Remove a record from another node |
| Accept Incoming | Receive and store records sent by other nodes |
| View Local | Display all records stored on current node |
| Delete All Local | Wipe all locally stored records |

## Key Learnings
- Distributed systems architecture and design
- Node discovery and peer-to-peer communication
- Remote CRUD operations across a network
- Socket programming for inter-node communication
- Data consistency in distributed environments

## Note
This project was developed for academic purposes to demonstrate 
distributed computing and storage concepts.
