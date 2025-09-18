# AOS Assignment 3: Peer-to-Peer File Sharing System
### Interim Submission

##### Name - Raj k jain
##### Roll number - 2025201036

implementation of a peer-to-peer distributed file sharing system. i have covered till  complete implementation of the user and group management functionaleties, supported by a synchronized two-tracker architecture.


## 1. Architectural Overview

The network is managed by two Tracker Servers which act as centralized coordinators for metadata but do not store any file data themselves.

**Trackers**: Two tracker servers run concurrently to provide redundancy and high availability.
**Clients**: client provides a command-line interface for all user and group operations.

The entire system is built using C++ and standard POSIX sockets for network communication, with a custom-designed protocol

---
## 2. Compilation and Execution

### File Structure
The project is organized into two main directories:
```
2025201036_A3
├── tracker/
│   ├── tracker.cpp
│   └── Makefile
├── client/
│   ├── client.cpp
│   └── Makefile
└── README.md
```

### Compilation

open the terminal from the directory `2025201036_A3`

1.  #### compile tracker
    ```bash
    cd tracker && make 
    ```
2.  #### compile client
    ```bash
    cd client && make 
    ```

### Execution
The system requires 4 separate terminals to run.

1.  ### Terminal 1: Start Tracker #1
    ```bash
    ./tracker tracker_info.txt 1
    ```
    Terminal o/p -> This tracker will listen on its configured port (e.g., 8080).

2.  ### Terminal 2: Start Tracker #2
    ```bash
    ./tracker/tracker tracker_info.txt 2
    ```
    Terminal o/p -> This tracker will listen on its port (e.g., 8081). The two trackers will automatically connect and synchronize with each other.

3.  ### Terminal 3: Start the Client 
    To connect to Tracker #1:
    ```bash
    ./client/client 127.0.0.1 8080
    ```
    You will see a success message and can begin entering commands.

4.  ### Terminal 4: Start the Client 
    To connect to Tracker #2:
    ```bash
    ./client/client 127.0.0.1 8081
    ```
    You will see a success message and can begin entering commands.

---
### Use these commands/scripts to test the above implementations.

#### A. Setup
1.  Compile and run the two trackers and one client as described in the **Execution** section. The client should be connected to Tracker #1 (port 8080).

#### B. User Management Test
1.  **Create users:**
    ```
    create_user raj 1234
    > SERVER: SUCCESS: User account created.
    create_user aman 5678
    > SERVER: SUCCESS: User account created.
    ```
2.  **Test login:**
    ```
    login raj 1234
    > SERVER: SUCCESS: Login successful.
    ```

#### C. Group Management Test
1.  **Create a group (as raj):**
    ```
    create_group grp1
    > SERVER: SUCCESS: Group created.
    ```
2.  **List groups:**
    ```
    list_groups
    > SERVER: Available groups:
    > grp1
    ```
3.  **Log out and log in as aman:**
    ```
    logout
    > SERVER: SUCCESS: Logged out.
    login aman 5678
    > SERVER: SUCCESS: Login successful.
    ```
4.  **Send join request (as aman):**
    ```
    join_group grp1
    > SERVER: SUCCESS: Request to join group sent.
    ```

#### D. Request Handling Test
1.  **Log out and log in as raj (owner):**
    ```
    logout
    > SERVER: SUCCESS: Logged out.
    login raj 1234
    > SERVER: SUCCESS: Login successful.
    ```
2.  **List and accept requests:**
    ```
    list_requests grp1
    > SERVER: Pending requests for grp1:
    > aman
    accept_request grp1 aman
    > SERVER: SUCCESS: User request accepted.
    ```

#### E. Final Synchronization Test
1.  In the 4th terminal
2.  
    ```bash
    ./client/client 127.0.0.1 8081
    ```
3.  In this new client, log in as a user created via Tracker #1:
    ```
    login raj 1234
    ```
    Result -  The login is successful (`SERVER: SUCCESS: Login successful.`).
    This proves that user data was correctly synchronized from Tracker #1 to Tracker #2.