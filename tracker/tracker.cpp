
#include <iostream>     // for cout,cin
#include <string>  // for string
#include <vector>       // for vector
#include <map>      // for map
#include <set>     // for set
#include <sstream>   // for stringstream
#include <thread>   // for thread
#include <mutex>   // for mutex
#include <cstring>  // for memset, memcpy
#include <unistd.h>  // for close, read, write, sleep
#include <sys/socket.h> // for socket functions
#include <netinet/in.h> // for sockaddr_in
#include <arpa/inet.h> // for inet_pton

using namespace std;

// Data Structures for State Management 
// User structure to hold passWord and login status
struct User {
    string passWord;
    bool isLoggedIn = false;
};

// Group struct to hold owner, members, and pending requests
struct Group {
    string owner;
    set<string> members;
    set<string> pendReq;
};

// Maps to hold users and groups
map<string, User> users;
map<string, Group> groups;

// Mutexes for thread safety     
mutex usersMutex;
mutex grpMutex;

// For peer tracker synchronization
int peerTrackerSocket = -1;
// Mutex for peer tracker socket
mutex peerSocketMutex;


//  Helper function for writing to sockets 
void sendResponse(int socket, const string& response) {
    write(socket, response.c_str(), response.length());
}

// Check if user is logged in
bool isLoggedIn(const string& user) {
    if (user.empty()) return false;
    lock_guard<mutex> lock(usersMutex);
    return users.count(user) && users[user].isLoggedIn;
}

// Function to synchronize commands with peer tracker
void syncWithPeer(const string& command) {
    lock_guard<mutex> lock(peerSocketMutex);
    if (peerTrackerSocket != -1) {
        if (write(peerTrackerSocket, command.c_str(), command.length()) < 0) {
            perror("ERROR ->  writing to peer socket");
        }
    }
}

//method to handle create user
void toHandleCreateUser(const vector<string>& args, int clientSocket, const string& orignalCmnd) {
    //checking numver arguments
    if (args.size() != 3) {
        sendResponse(clientSocket, "ERROR -> : Usage: create_user <user_id> <passWord>\n");
        return;
    }

    string userId = args[1];
    string passWord = args[2];  
    lock_guard<mutex> lock(usersMutex);
    if (users.count(userId)) {
        sendResponse(clientSocket, "ERROR -> : User already exists.\n");
    } else {
        users[userId] = User{passWord, false};
        sendResponse(clientSocket, "SUCCESS: User account created.\n");
        if (orignalCmnd == "create_user") {
            string syncCmnd = "SYNC_CREATE_USER:" + userId + ":" + passWord + "\n";
            syncWithPeer(syncCmnd);
        }
    }
}


// Function to connect to peer tracker
int connectToPeer(const char* ip, int port) {
    int sock = 0;
    struct sockaddr_in serv_addr;
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) return -1;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip, &serv_addr.sin_addr) <= 0) return -1;
    while (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        // cerr << "Connection to peer tracker failed, retrying in 3 seconds..." << endl;
        sleep(3);
    }
    cout << "Success connected to peer tracker." << endl;
    return sock;
}

// fundtion to handl e logins
void handleLogin(const vector<string>& args, int clientSocket, string& currUsr) {
    //checkint number of arguments
    if (args.size() != 3) {
        sendResponse(clientSocket, "ERROR -> : Usage: login <user_id> <passWord>\n");
        return;
    }

    string userId = args[1];
    string passWord = args[2];
    lock_guard<mutex> lock(usersMutex);
    if (users.count(userId) && users[userId].passWord == passWord) {
        if (users[userId].isLoggedIn) {
            sendResponse(clientSocket, "ERROR -> : User already logged in.\n");
        } else {
            users[userId].isLoggedIn = true;
            currUsr = userId;
            sendResponse(clientSocket, "SUCCESS: Login successful.\n");
        }
    } else {
        sendResponse(clientSocket, "ERROR -> : Invalid credentials.\n");
    }
}

// Function to connect to tracker (used by client)
void handleLogout(int clientSocket, string& currUsr) {
    if (!isLoggedIn(currUsr)) {
        sendResponse(clientSocket, "ERROR -> : You are not logged in.\n");
        return;
    }
    lock_guard<mutex> lock(usersMutex);
    users[currUsr].isLoggedIn = false;
    currUsr = "";
    sendResponse(clientSocket, "SUCCESS: Logged out.\n");
}


void handleCreateGrp(const vector<string>& args, int clientSocket, const string& currUsr, const string& orignalCmnd) {
    if (!isLoggedIn(currUsr)) {
        sendResponse(clientSocket, "ERROR -> : You must be logged in to create a group.\n");
        return;
    }
    if (args.size() < 2) { // Allow for SYNC command with more args
        sendResponse(clientSocket, "ERROR -> : Usage: create_group <group_id>\n");
        return;
    }
    string grpId = args[1];
    lock_guard<mutex> lock(grpMutex);
    if (groups.count(grpId)) {
        sendResponse(clientSocket, "ERROR -> : Group already exists.\n");
    } else {
        string owner = (orignalCmnd == "create_group") ? currUsr : args[2];
        Group newGrp;
        newGrp.owner = owner;
        newGrp.members.insert(owner);
        groups[grpId] = newGrp;
        if (orignalCmnd == "create_group") {
            sendResponse(clientSocket, "SUCCESS: Group created.\n");
            string syncCmnd = "SYNC_CREATE_GROUP:" + grpId + ":" + currUsr + "\n";
            syncWithPeer(syncCmnd);
        }
    }
}

void handleJoinGrp(const vector<string>& args, int clientSocket, const string& currUsr, const string& orignalCmnd) {
    if (orignalCmnd == "join_group" && !isLoggedIn(currUsr)) {
        sendResponse(clientSocket, "ERROR -> : You must be logged in to join a group.\n");
        return;
    }
    if (args.size() < 2) {
        sendResponse(clientSocket, "ERROR -> : Usage: join_group <group_id>\n");
        return;
    }
    string grpId = args[1];
    string userId = (orignalCmnd == "join_group") ? currUsr : args[2];
    lock_guard<mutex> lock(grpMutex);
    if (!groups.count(grpId)) {
        if(orignalCmnd == "join_group") sendResponse(clientSocket, "ERROR -> : Group does not exist.\n");
    } else {
        groups[grpId].pendReq.insert(userId);
        if (orignalCmnd == "join_group") {
            sendResponse(clientSocket, "SUCCESS: Request to join group sent.\n");
            string syncCmnd = "SYNC_JOIN_GROUP:" + grpId + ":" + currUsr + "\n";
            syncWithPeer(syncCmnd);
        }
    }
}

void handleLeaveGrp(const vector<string>& args, int clientSocket, const string& currUsr, const string& orignalCmnd) {
    if (orignalCmnd == "leave_group" && !isLoggedIn(currUsr)) {
        sendResponse(clientSocket, "ERROR -> : You must be logged in to leave a group.\n");
        return;
    }
    if (args.size() < 2) {
        sendResponse(clientSocket, "ERROR -> : Usage: leave_group <group_id>\n");
        return;
    }
    string grpId = args[1];
    string userId = (orignalCmnd == "leave_group") ? currUsr : args[2];
    lock_guard<mutex> lock(grpMutex);

    if (!groups.count(grpId)) {
        if (orignalCmnd == "leave_group") sendResponse(clientSocket, "ERROR -> : Group does not exist.\n");
    } else if (groups[grpId].owner == userId) {
        // For simplicity, owners cannot leave. You could implement group deletion instead.
        if (orignalCmnd == "leave_group") sendResponse(clientSocket, "ERROR -> : Owner cannot leave the group.\n");
    } else if (groups[grpId].members.find(userId) == groups[grpId].members.end()) {
        if (orignalCmnd == "leave_group") sendResponse(clientSocket, "ERROR -> : You are not a member of this group.\n");
    } else {
        groups[grpId].members.erase(userId);
        if (orignalCmnd == "leave_group") {
            sendResponse(clientSocket, "SUCCESS: You have left the group.\n");
            string syncCmnd = "SYNC_LEAVE_GROUP:" + grpId + ":" + currUsr + "\n";
            syncWithPeer(syncCmnd);
        }
    }
}

// function to display the lists req
void handleListReq(const vector<string>& args, int clientSocket, const string& currUsr) {
    //check for the login 
    if (!isLoggedIn(currUsr)) {
        sendResponse(clientSocket, "ERROR -> : You must be logged in.\n");
        return;
    }
    //check for the arguments
    if (args.size() != 2) {
        sendResponse(clientSocket, "ERROR -> : Usage: list_requests <group_id>\n");
        return;
    }

    string grpId = args[1];
    lock_guard<mutex> lock(grpMutex);
    //check for the group exists or not 
    if (!groups.count(grpId)) {
        sendResponse(clientSocket, "ERROR -> : Group does not exist.\n");
    } 
    
    else if (groups[grpId].owner != currUsr) {
        sendResponse(clientSocket, "ERROR -> : You are not the owner of this group.\n");
    } 
    else {
        string response = "Pending requests for " + grpId + ":\n";
        if (groups[grpId].pendReq.empty()) {
            response += "No pending requests.\n";
        } else {
            for (const auto& user : groups[grpId].pendReq) {
                response += user + "\n";
            }
        }
        sendResponse(clientSocket, response);
    }
}

void handleAcptReq(const vector<string>& args, int clientSocket, const string& currUsr, const string& orignalCmnd) {
    if (orignalCmnd == "accept_request" && !isLoggedIn(currUsr)) {
        sendResponse(clientSocket, "ERROR -> : You must be logged in.\n");
        return;
    }
    if (args.size() != 3) {
        sendResponse(clientSocket, "ERROR -> : Usage: accept_request <group_id> <user_id>\n");
        return;
    }

    string grpId = args[1];
    string userIdToAccept = args[2];
    lock_guard<mutex> lock(grpMutex);

    if (!groups.count(grpId)) {
        if (orignalCmnd == "accept_request") sendResponse(clientSocket, "ERROR -> : Group does not exist.\n");
    } else if (groups[grpId].owner != currUsr && orignalCmnd == "accept_request") {
        sendResponse(clientSocket, "ERROR -> : You are not the owner of this group.\n");
    } else if (groups[grpId].pendReq.find(userIdToAccept) == groups[grpId].pendReq.end()) {
        if (orignalCmnd == "accept_request") sendResponse(clientSocket, "ERROR -> : User has not requested to join this group.\n");
    } else {
        groups[grpId].pendReq.erase(userIdToAccept);
        groups[grpId].members.insert(userIdToAccept);
        if (orignalCmnd == "accept_request") {
            sendResponse(clientSocket, "SUCCESS: User request accepted.\n");
            string syncCmnd = "SYNC_ACCEPT_REQUEST:" + grpId + ":" + userIdToAccept + "\n";
            syncWithPeer(syncCmnd);
        }
    }
}


void handleListGrp(int clientSocket) {
    lock_guard<mutex> lock(grpMutex);
    string response = "Available groups:\n";
    if (groups.empty()) {
        response += "No groups found.\n";
    } else {
        for (const auto& [key, val] : groups) {
            response += key + "\n";
        }
    }
    sendResponse(clientSocket, response);
}


//  Client Handling Function (runs in a new thread) 
// In tracker/tracker.cpp

//  Client Handling Function (runs in a new thread) 
// This is a robust version that handles TCP streams correctly.
void handleClient(int clientSocket) {
    char buffer[1024];
    string receivedData;
    string currUsr = ""; // Tracks the logged-in user for this session

    while (true) {
        int bytes_read = read(clientSocket, buffer, 1023);

        if (bytes_read <= 0) {
            // Client disconnected or an error occurred
            cout << "Client disconected." << endl;
            if (!currUsr.empty()) {
                lock_guard<mutex> lock(usersMutex);
                if(users.count(currUsr)) {
                    users[currUsr].isLoggedIn = false;
                }
            }
            close(clientSocket);
            return;
        }

        buffer[bytes_read] = '\0'; // Null-terminate the received data
        receivedData.append(buffer);

        size_t pos;
        // Process all complete commands (ending in '\n') in our buffer
        while ((pos = receivedData.find('\n')) != string::npos) {
            string cmndLine = receivedData.substr(0, pos);
            receivedData.erase(0, pos + 1);

            // Now, parse the clean cmndLine
            stringstream ss(cmndLine);
            string part;
            vector<string> args;
            while(getline(ss, part, ':')) {
                // Trim potential '\r' from Windows clients
                if (!part.empty() && part.back() == '\r') {
                    part.pop_back();
                }
                args.push_back(part);
            }

            if (args.empty() || args[0].empty()) {
                continue;
            }

            string command = args[0];
            cout << "Received command: " << command << endl;

            //  Command Dispatcher (this part remains the same) 
            if (command == "create_user" || command == "SYNC_CREATE_USER") {
                toHandleCreateUser(args, clientSocket, command);
            } else if (command == "login") {
                handleLogin(args, clientSocket, currUsr);
            } else if (command == "create_group" || command == "SYNC_CREATE_GROUP") {
                handleCreateGrp(args, clientSocket, currUsr, command);
            } else if (command == "join_group" || command == "SYNC_JOIN_GROUP") {
                handleJoinGrp(args, clientSocket, currUsr, command);
            } else if (command == "leave_group" || command == "SYNC_LEAVE_GROUP") {
                handleLeaveGrp(args, clientSocket, currUsr, command);
            } else if (command == "list_requests") {
                handleListReq(args, clientSocket, currUsr);
            } else if (command == "accept_request" || command == "SYNC_ACCEPT_REQUEST") {
                handleAcptReq(args, clientSocket, currUsr, command);
            } else if (command == "list_groups") {
                handleListGrp(clientSocket);
            } else if (command == "logout") {
                handleLogout(clientSocket, currUsr);
            } else {
                sendResponse(clientSocket, "ERROR -> : Unknown command.\n");
            }
        }
    }
}


int main(int argc, char *argv[]) {
    if (argc < 3) {
        cerr << "Usage: ./tracker tracker_info.txt <tracker_no>\n";
        return 1;
    }
    string tracker_num_str = argv[2];
    int my_port, peer_port;
    string peer_ip = "127.0.0.1";
    if (tracker_num_str == "1") {
        my_port = 8080; peer_port = 8081;
    } else if (tracker_num_str == "2") {
        my_port = 8081; peer_port = 8080;
    } else {
        cerr << "Error: Tracker numbr must be 1 or 2.\n"; return 1;
    }
    
    int serverFd;
    struct sockaddr_in address;
    int opt = 1;
    socklen_t addrlen = sizeof(address);

    if ((serverFd = socket(AF_INET, SOCK_STREAM, 0)) == 0) { 
        perror("socket failed"); 
        exit(EXIT_FAILURE); 
    }
    if (setsockopt(serverFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) { 
        perror("setsockopt"); 
        exit(EXIT_FAILURE); 
    }
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(my_port);

    if (bind(serverFd, (struct sockaddr *)&address, sizeof(address)) < 0) { 
        perror("bind failed"); 
        exit(EXIT_FAILURE); 
    }

    if (listen(serverFd, 10) < 0) {
         perror("listen"); 
         exit(EXIT_FAILURE); 
    }
    

    cout << "Tracker #" << tracker_num_str << " listening on port " << my_port << endl;

    thread peer_connector([&]() {
        int sock = connectToPeer(peer_ip.c_str(), peer_port);
        lock_guard<mutex> lock(peerSocketMutex);
        peerTrackerSocket = sock;
    });
    peer_connector.detach();

    while (true) {
        int nSocket;
        if ((nSocket = accept(serverFd, (struct sockaddr *)&address, &addrlen)) < 0) {
            perror("accept"); continue;
        }
        thread clientThread(handleClient, nSocket);
        clientThread.detach();
    }
    return 0;
}