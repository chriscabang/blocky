# QuteChain: A Post-Quantum Secure Blockchain for Raspberry Pi

               +----------------------+
               |      startchain       |  --> Initializes the blockchain
               +----------------------+
                          |
                          v
     +------------------------------------------+
     |               blockchain.c               |
     |  - Stores & manages blocks               |
     |  - Links blocks using previous_hash      |
     |  - Calls consensus to validate blocks    |
     +------------------------------------------+
                          |
                          v
     +------------------------------------------+
     |             transaction.c                |
     |  - Manages transactions in a block      |
     |  - Ensures valid transfers               |
     +------------------------------------------+
                          |
                          v
     +------------------------------------------+
     |                consensus.c               |
     |  - Uses PoW or PoS to validate blocks    |
     |  - Calls crypto functions for hashing    |
     +------------------------------------------+
                          |
                          v
     +------------------------------------------+
     |                storage.c                 |
     |  - Saves blockchain to a file/db         |
     |  - Loads blockchain on startup           |
     +------------------------------------------+
                          ^
                          |
     +------------------------------------------+
     |                network.c                 |
     |  - Sends & receives blocks over TLS      |
     |  - Broadcasts transactions               |
     +------------------------------------------+
                          |
                          v
               +----------------------+
               |      endchain         |  --> Stops the blockchain
               +----------------------+

## Overview

QuteChain is a **minimal, lightweight, and post-quantum secure blockchain** implementation designed to run on **Raspberry Pi** and other embedded devices. It follows the principles of the **Bitcoin Whitepaper** while incorporating **quantum-resistant cryptography** for security.

QuteChain replaces traditional **ECDSA signatures** with **Merkle Signature Scheme (MSS)** from the **liboqs library** and uses **TLS with post-quantum key exchange (Kyber768)** for secure communication.

---

## Architecture Overview

QuteChain consists of **four key layers**:

```
+-------------------------------+
|        Network Layer         |   <-- Handles TLS-based communication
+-------------------------------+
|    Blockchain Layer          |   <-- Stores and validates blocks
+-------------------------------+
|   Transaction Layer          |   <-- Handles transaction validation
+-------------------------------+
|  Proof-of-Work (PoW) Layer   |   <-- Manages mining and consensus
+-------------------------------+
```

### **1. Blockchain Layer**

- Manages the **linked list of blocks**.
- Ensures **block validity** before adding them.
- Uses **Merkle Trees** to store transaction hashes.

### **2. Transaction Layer**

- Defines the structure of a **transaction**.
- Verifies **signatures** using **Merkle Signature Scheme (MSS)**.
- Prevents **double spending**.

### **3. Proof-of-Work (PoW) Layer**

- Implements **SHA-256-based PoW**.
- Miners must find a hash that meets the **difficulty target**.
- Adjusts difficulty dynamically.

### **4. Network Layer (TLS + PQC)**

- Handles **peer-to-peer communication** securely.
- Uses **TLS with Kyber768** key exchange.
- Broadcasts **new blocks** to the network.

---

## **Building and Running QuteChain**

### **Prerequisites**

1. **Install OpenSSL and liboqs** (Post-Quantum Crypto):
   ```sh
   sudo apt update
   sudo apt install libssl-dev
   git clone https://github.com/open-quantum-safe/liboqs.git
   cd liboqs && cmake . && make && sudo make install
   ```
2. **Install GCC** (If not installed):
   ```sh
   sudo apt install gcc make
   ```

### **Building QuteChain**

Compile the blockchain source code:

```sh
make all
```

This generates the `blockchain` executable.

### **Running the Blockchain**

Start the blockchain node:

```sh
./blockchain
```

---

## **How QuteChain Works**

### **1. Creating a New Block**

- Transactions are collected and **added to a block**.
- The block is hashed using **SHA-256**.
- A miner solves the **PoW** and broadcasts the block.

### **2. Verifying a Block**

- Nodes validate **Merkle signatures** of transactions.
- They ensure the block hash meets the **difficulty**.
- If valid, the block is added to the **blockchain**.

### **3. Secure Communication**

- Nodes use **TLS with Kyber768** to communicate.
- Prevents **MITM (Man-in-the-Middle) attacks**.

---

## **Future Enhancements**

- Add **Smart Contracts** support.
- Improve **efficiency on low-power devices**.
- Integrate **Post-Quantum Digital Signatures** like **Dilithium**.

---

## **Contributors**

- **QuteChain Development Team**

For questions or contributions, feel free to reach out!


