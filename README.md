# Blockchain Transaction Verifier

A POSIX-compliant C program that validates blockchain transactions using shared memory and message queues. The system processes encrypted wallet data, identifies suspicious transactions, and communicates results to a helper process.

## Overview

This project simulates a blockchain verification system where each block contains encrypted wallet IDs stored in shared memory. A helper process provides decryption keys and validation thresholds.  

The program decrypts wallet IDs, scans a large transaction dataset, identifies suspicious transactions, and computes aggregate values efficiently.

## Key Features

- Shared Memory (shmget, shmat) for blockchain data access  
- Message Queues (msgget, msgsnd, msgrcv) for inter-process communication  
- Efficient transaction lookup using custom hash map  
- Rotational cipher decryption for wallet IDs  
- Optimized parsing of large transaction files  
- Handles duplicate wallets and edge cases correctly  

## How It Works

1. Reads input configuration (`input_t.txt`)  
2. Connects to shared memory containing blockchain data  
3. Receives security parameters from helper process  
4. Decrypts wallet IDs using rotational cipher  
5. Finds all related transactions (sender/receiver)  
6. Filters suspicious transactions based on leading zeros  
7. Computes total transaction amount  
8. Sends result back via message queue  

## File Structure

```
.
├── solution.c # Main implementation
├── helper-program-release.c # Helper process
├── testcases/ # Input + transaction files
├── README.md
```

## How to Run

Make sure testcases are in the same directory or correctly referenced.

Compile:

```
gcc helper-program-release.c -lpthread -o helper
gcc solution.c -lpthread -o solution
```

Run:

```
./helper <TESTCASE_NUMBER>
```

Example:

```
./helper 1
```

## Key Learnings

- Working with low-level IPC mechanisms in C  
- Designing efficient data structures for large datasets  
- Optimizing file I/O and lookup operations  
- Building robust systems under strict constraints  

## Notes

This project was developed as part of an Operating Systems assignment and focuses on correctness, efficiency, and POSIX compliance.
