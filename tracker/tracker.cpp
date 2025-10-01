// tracker.cpp
// Build: g++ -std=c++17 -pthread tracker.cpp -o tracker
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <csignal>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using namespace std;

struct User {
    string passWord;
    bool isLoggedIn = false;
};

struct FileInfo {
    string filename;
    long long file_size = 0;
    string combined_hashes; // concatenation of 40-hex SHA1 per piece
    set<string> seeders;    // "IP:PORT"
};

struct Group {
    string owner;
    set<string> members;
    set<string> pendReq;
    map<string, FileInfo> files; // filename (basename) -> FileInfo
};

static map<string, User> users;
static map<string, Group> groups;

static mutex usersMutex;
static mutex grpMutex;

static int peerTrackerSocket = -1;
static mutex peerSocketMutex;

static void sendResponse(int socket_fd, const string& response) {
    (void)write(socket_fd, response.c_str(), response.size());
}

static bool isLoggedInUser(const string& user) {
    if (user.empty()) return false;
    lock_guard<mutex> lock(usersMutex);
    auto it = users.find(user);
    return it != users.end() && it->second.isLoggedIn;
}

static void syncWithPeer(const string& cmnd) {
    lock_guard<mutex> lock(peerSocketMutex);
    if (peerTrackerSocket != -1) {
        if (write(peerTrackerSocket, cmnd.c_str(), cmnd.size()) < 0) {
            cout << "[SYNC] Warning: Failed to sync with peer tracker" << endl;
        }
    }
}

// create_user:<user>:<pass>
static void handleCreateUser(const vector<string>& args, int clientSocket, const string& originalCmd) {
    if (args.size() != 3) { sendResponse(clientSocket, "ERROR: Usage: create_user:<user>:<pass>\n"); return; }
    string userId = args[1], passWord = args[2];

    {
        lock_guard<mutex> lock(usersMutex);
        if (users.count(userId)) { sendResponse(clientSocket, "ERROR: User already exists.\n"); return; }
        users[userId] = User{passWord, false};
    }
    if (originalCmd == "create_user") {
        sendResponse(clientSocket, "SUCCESS: User account created.\n");
        syncWithPeer("SYNC_CREATE_USER:" + userId + ":" + passWord + "\n");
    }
}

// login:<user>:<pass>
static void handleLogin(const vector<string>& args, int clientSocket, string& currUsr) {
    if (args.size() != 3) { sendResponse(clientSocket, "ERROR: Usage: login:<user>:<pass>\n"); return; }
    string userId = args[1], passWord = args[2];

    lock_guard<mutex> lock(usersMutex);
    auto it = users.find(userId);
    if (it != users.end() && it->second.passWord == passWord) {
        if (it->second.isLoggedIn) sendResponse(clientSocket, "ERROR: User already logged in.\n");
        else { it->second.isLoggedIn = true; currUsr = userId; sendResponse(clientSocket, "SUCCESS: Login successful.\n"); }
    } else sendResponse(clientSocket, "ERROR: Invalid credentials.\n");
}

// logout
static void handleLogout(int clientSocket, string& currUsr) {
    if (!isLoggedInUser(currUsr)) { sendResponse(clientSocket, "ERROR: You are not logged in.\n"); return; }
    {
        lock_guard<mutex> lock(usersMutex);
        auto it = users.find(currUsr);
        if (it != users.end()) it->second.isLoggedIn = false;
    }
    currUsr.clear();
    sendResponse(clientSocket, "SUCCESS: Logged out.\n");
}

// create_group:<group_id>
static void handleCreateGroup(const vector<string>& args, int clientSocket, const string& currUsr, const string& originalCmd) {
    if (originalCmd == "create_group" && !isLoggedInUser(currUsr)) { sendResponse(clientSocket, "ERROR: Login to create a group.\n"); return; }
    if (args.size() < 2) { sendResponse(clientSocket, "ERROR: Usage: create_group:<group_id>\n"); return; }
    string grpId = args[1];

    lock_guard<mutex> lock(grpMutex);
    if (groups.count(grpId)) { if (originalCmd == "create_group") sendResponse(clientSocket, "ERROR: Group already exists.\n"); return; }
    string owner = (originalCmd == "create_group") ? currUsr : (args.size() >= 3 ? args[2] : "");
    Group newGrp;
    newGrp.owner = owner;
    if (!owner.empty()) newGrp.members.insert(owner);
    groups[grpId] = move(newGrp);

    if (originalCmd == "create_group") {
        sendResponse(clientSocket, "SUCCESS: Group created.\n");
        syncWithPeer("SYNC_CREATE_GROUP:" + grpId + ":" + currUsr + "\n");
    }
}

// join_group:<group_id>
static void handleJoinGroup(const vector<string>& args, int clientSocket, const string& currUsr, const string& originalCmd) {
    if (originalCmd == "join_group" && !isLoggedInUser(currUsr)) { sendResponse(clientSocket, "ERROR: You must be logged in to join a group.\n"); return; }
    if (args.size() < 2) { sendResponse(clientSocket, "ERROR: Usage: join_group:<group_id>\n"); return; }
    string grpId = args[1];
    string userId = (originalCmd == "join_group") ? currUsr : (args.size() >= 3 ? args[2] : "");

    lock_guard<mutex> lock(grpMutex);
    if (!groups.count(grpId)) { if (originalCmd == "join_group") sendResponse(clientSocket, "ERROR: Group does not exist.\n"); return; }
    groups[grpId].pendReq.insert(userId);
    if (originalCmd == "join_group") {
        sendResponse(clientSocket, "SUCCESS: Request to join group sent.\n");
        syncWithPeer("SYNC_JOIN_GROUP:" + grpId + ":" + currUsr + "\n");
    }
}

// leave_group:<group_id>
static void handleLeaveGroup(const vector<string>& args, int clientSocket, const string& currUsr, const string& originalCmd) {
    if (originalCmd == "leave_group" && !isLoggedInUser(currUsr)) { sendResponse(clientSocket, "ERROR: You must be logged in to leave a group.\n"); return; }
    if (args.size() < 2) { sendResponse(clientSocket, "ERROR: Usage: leave_group:<group_id>\n"); return; }
    string grpId = args[1];
    string userId = (originalCmd == "leave_group") ? currUsr : (args.size() >= 3 ? args[2] : "");

    lock_guard<mutex> lock(grpMutex);
    if (!groups.count(grpId)) { if (originalCmd == "leave_group") sendResponse(clientSocket, "ERROR: Group does not exist.\n"); return; }
    if (groups[grpId].owner == userId) { if (originalCmd == "leave_group") sendResponse(clientSocket, "ERROR: Owner cannot leave the group.\n"); return; }
    if (!groups[grpId].members.count(userId)) { if (originalCmd == "leave_group") sendResponse(clientSocket, "ERROR: You are not a member of this group.\n"); return; }
    groups[grpId].members.erase(userId);
    if (originalCmd == "leave_group") {
        sendResponse(clientSocket, "SUCCESS: You have left the group.\n");
        syncWithPeer("SYNC_LEAVE_GROUP:" + grpId + ":" + currUsr + "\n");
    }
}

// list_requests:<group_id>
static void handleListReq(const vector<string>& args, int clientSocket, const string& currUsr) {
    if (!isLoggedInUser(currUsr)) { sendResponse(clientSocket, "ERROR: You must be logged in.\n"); return; }
    if (args.size() != 2) { sendResponse(clientSocket, "ERROR: Usage: list_requests:<group_id>\n"); return; }
    string grpId = args[1];

    lock_guard<mutex> lock(grpMutex);
    if (!groups.count(grpId)) { sendResponse(clientSocket, "ERROR: Group does not exist.\n"); return; }
    if (groups[grpId].owner != currUsr) { sendResponse(clientSocket, "ERROR: You are not the owner of this group.\n"); return; }
    string response = "Pending requests for " + grpId + ":\n";
    if (groups[grpId].pendReq.empty()) response += "No pending requests.\n";
    else for (const auto& user : groups[grpId].pendReq) response += user + "\n";
    sendResponse(clientSocket, response);
}

// accept_request:<group_id>:<user>
static void handleAcceptReq(const vector<string>& args, int clientSocket, const string& currUsr, const string& originalCmd) {
    if (originalCmd == "accept_request" && !isLoggedInUser(currUsr)) { sendResponse(clientSocket, "ERROR: You must be logged in.\n"); return; }
    if (args.size() != 3) { sendResponse(clientSocket, "ERROR: Usage: accept_request:<group_id>:<user>\n"); return; }
    string grpId = args[1], userIdToAccept = args[2];

    lock_guard<mutex> lock(grpMutex);
    if (!groups.count(grpId)) { if (originalCmd == "accept_request") sendResponse(clientSocket, "ERROR: Group does not exist.\n"); return; }
    if (originalCmd == "accept_request" && groups[grpId].owner != currUsr) { sendResponse(clientSocket, "ERROR: You are not the owner of this group.\n"); return; }
    if (!groups[grpId].pendReq.count(userIdToAccept)) { if (originalCmd == "accept_request") sendResponse(clientSocket, "ERROR: User has not requested to join this group.\n"); return; }
    groups[grpId].pendReq.erase(userIdToAccept);
    groups[grpId].members.insert(userIdToAccept);

    if (originalCmd == "accept_request") {
        sendResponse(clientSocket, "SUCCESS: User request accepted.\n");
        syncWithPeer("SYNC_ACCEPT_REQUEST:" + grpId + ":" + userIdToAccept + "\n");
    }
}

// list_groups
static void handleListGroups(int clientSocket) {
    lock_guard<mutex> lock(grpMutex);
    string response = "Available groups:\n";
    if (groups.empty()) response += "No groups found.\n";
    else for (const auto& [key, _] : groups) response += key + "\n";
    sendResponse(clientSocket, response);
}

// upload_file:<group_id>:<file_path>:<file_size>:<client_listen_port>:<combined_hashes>
// SYNC_UPLOAD_FILE:<group_id>:<filename>:<file_size>:<seeder_ip:port>:<combined_hashes>
static void handleUploadFile(const vector<string>& args, int clientSocket, const string& currUsr, const string& originalCmd) {
    if (originalCmd == "upload_file" && !isLoggedInUser(currUsr)) { sendResponse(clientSocket, "ERROR: You must be logged in to upload a file.\n"); return; }
    if (args.size() < 6) { sendResponse(clientSocket, "ERROR: Invalid upload command format.\n"); return; }
    string grpId = args[1];
    string file_path_or_name = args[2];
    long long file_size = 0;
    try { file_size = stoll(args[3]); } catch (...) { sendResponse(clientSocket, "ERROR: Invalid file size.\n"); return; }

    {
        lock_guard<mutex> lock(grpMutex);
        if (!groups.count(grpId)) { if (originalCmd == "upload_file") sendResponse(clientSocket, "ERROR: Group does not exist.\n"); return; }
        if (originalCmd == "upload_file" && !groups[grpId].members.count(currUsr)) { sendResponse(clientSocket, "ERROR: You are not a member of this group.\n"); return; }
    }

    string seeder_address, combined_hashes;
    if (originalCmd == "upload_file") {
        struct sockaddr_in client_addr{};
        socklen_t len = sizeof(client_addr);
        if (getpeername(clientSocket, (struct sockaddr*)&client_addr, &len) < 0) { sendResponse(clientSocket, "ERROR: getpeername failed.\n"); return; }
        string client_ip = inet_ntoa(client_addr.sin_addr);
        string client_listen_port = args[4];
        seeder_address = client_ip + ":" + client_listen_port;
        combined_hashes = args[5];
    } else {
        seeder_address = args[4];
        combined_hashes = args[5];
    }

    // Normalize to basename
    size_t p = file_path_or_name.find_last_of("/\\");
    string fname = (p == string::npos) ? file_path_or_name : file_path_or_name.substr(p + 1);

    {
        lock_guard<mutex> lock(grpMutex);
        FileInfo& fi = groups[grpId].files[fname];
        fi.filename = fname;
        fi.file_size = file_size;
        fi.combined_hashes = combined_hashes;
        fi.seeders.insert(seeder_address);
    }

    if (originalCmd == "upload_file") {
        sendResponse(clientSocket, "SUCCESS: File shared successfully.\n");
        string syncCmd = "SYNC_UPLOAD_FILE:" + grpId + ":" + fname + ":" + to_string(file_size) + ":" + seeder_address + ":" + combined_hashes + "\n";
        syncWithPeer(syncCmd);
    }
}

// list_files:<group_id>
static void handleListFiles(const vector<string>& args, int clientSocket, const string& currUsr) {
    if (!isLoggedInUser(currUsr)) { sendResponse(clientSocket, "ERROR: You must be logged in.\n"); return; }
    if (args.size() != 2) { sendResponse(clientSocket, "ERROR: Usage: list_files:<group_id>\n"); return; }
    string grpId = args[1];

    lock_guard<mutex> lock(grpMutex);
    if (!groups.count(grpId)) { sendResponse(clientSocket, "ERROR: Group does not exist.\n"); return; }
    string response = "Files in " + grpId + ":\n";
    if (groups[grpId].files.empty()) response += "No files in this group.\n";
    else for (auto const& [fname, _] : groups[grpId].files) response += fname + "\n";
    sendResponse(clientSocket, response);
}

// download_file:<group_id>:<filename>
// -> tracker responds: "<file_size>:<combined_hashes>:<ip1:port1>:<ip2:port2>...\n"
static void handleDownloadFile(const vector<string>& args, int clientSocket, const string& currUsr) {
    if (!isLoggedInUser(currUsr)) { sendResponse(clientSocket, "ERROR: You must be logged in.\n"); return; }
    if (args.size() != 3) { sendResponse(clientSocket, "ERROR: Usage: download_file:<group_id>:<filename>\n"); return; }
    string grpId = args[1], filename = args[2];

    lock_guard<mutex> lock(grpMutex);
    if (!groups.count(grpId) || !groups[grpId].files.count(filename)) { sendResponse(clientSocket, "ERROR: File not found in this group.\n"); return; }
    FileInfo& f = groups[grpId].files[filename];
    string response = to_string(f.file_size) + ":" + f.combined_hashes;
    for (const auto& seeder : f.seeders) response += ":" + seeder;
    response += "\n";
    sendResponse(clientSocket, response);
}

// stop_share:<group_id>:<filename>
// SYNC_STOP_SHARE:<group_id>:<filename>:<ip>
static void handleStopShare(const vector<string>& args, int clientSocket, const string& currUsr, const string& originalCmd) {
    if (originalCmd == "stop_share" && !isLoggedInUser(currUsr)) { sendResponse(clientSocket, "ERROR: You must be logged in.\n"); return; }
    if (args.size() < 3) { sendResponse(clientSocket, "ERROR: Usage: stop_share:<group_id>:<filename>\n"); return; }
    string grpId = args[1], filename = args[2];

    string seeder_to_remove_ip;
    if (originalCmd == "stop_share") {
        struct sockaddr_in client_addr{};
        socklen_t len = sizeof(client_addr);
        if (getpeername(clientSocket, (struct sockaddr*)&client_addr, &len) < 0) { sendResponse(clientSocket, "ERROR: getpeername failed.\n"); return; }
        seeder_to_remove_ip = inet_ntoa(client_addr.sin_addr);
    } else {
        if (args.size() < 4) return;
        seeder_to_remove_ip = args[3];
    }

    bool removed = false;
    {
        lock_guard<mutex> lock(grpMutex);
        if (groups.count(grpId) && groups[grpId].files.count(filename)) {
            auto& seeders = groups[grpId].files[filename].seeders;
            for (auto it = seeders.begin(); it != seeders.end();) {
                if (it->rfind(seeder_to_remove_ip, 0) == 0 || it->find(seeder_to_remove_ip) != string::npos) {
                    it = seeders.erase(it); removed = true;
                } else ++it;
            }
        }
    }

    if (removed) {
        if (originalCmd == "stop_share") {
            sendResponse(clientSocket, "SUCCESS: You are no longer sharing the file.\n");
            syncWithPeer("SYNC_STOP_SHARE:" + grpId + ":" + filename + ":" + seeder_to_remove_ip + "\n");
        }
    } else {
        if (originalCmd == "stop_share") sendResponse(clientSocket, "ERROR: You are not sharing that file in this group.\n");
    }
}

static void handleClient(int clientSocket) {
    char buffer[4096];
    string receivedData;
    string currUsr;

    while (true) {
        int bytesRead = read(clientSocket, buffer, sizeof(buffer) - 1);
        if (bytesRead <= 0) {
            if (!currUsr.empty()) {
                lock_guard<mutex> lock(usersMutex);
                auto it = users.find(currUsr);
                if (it != users.end()) it->second.isLoggedIn = false;
            }
            close(clientSocket);
            return;
        }
        buffer[bytesRead] = '\0';
        receivedData.append(buffer);

        size_t pos;
        while ((pos = receivedData.find('\n')) != string::npos) {
            string cmdLine = receivedData.substr(0, pos);
            receivedData.erase(0, pos + 1);

            stringstream ss(cmdLine);
            string part;
            vector<string> args;
            while (getline(ss, part, ':')) {
                if (!part.empty() && part.back() == '\r') part.pop_back();
                args.push_back(part);
            }
            if (args.empty() || args[0].empty()) continue;
            const string& cmd = args[0];

            if (cmd == "create_user" || cmd == "SYNC_CREATE_USER")
                handleCreateUser(args, clientSocket, cmd);
            else if (cmd == "login")
                handleLogin(args, clientSocket, currUsr);
            else if (cmd == "create_group" || cmd == "SYNC_CREATE_GROUP")
                handleCreateGroup(args, clientSocket, currUsr, cmd);
            else if (cmd == "join_group" || cmd == "SYNC_JOIN_GROUP")
                handleJoinGroup(args, clientSocket, currUsr, cmd);
            else if (cmd == "leave_group" || cmd == "SYNC_LEAVE_GROUP")
                handleLeaveGroup(args, clientSocket, currUsr, cmd);
            else if (cmd == "list_requests")
                handleListReq(args, clientSocket, currUsr);
            else if (cmd == "accept_request" || cmd == "SYNC_ACCEPT_REQUEST")
                handleAcceptReq(args, clientSocket, currUsr, cmd);
            else if (cmd == "list_groups")
                handleListGroups(clientSocket);
            else if (cmd == "upload_file" || cmd == "SYNC_UPLOAD_FILE")
                handleUploadFile(args, clientSocket, currUsr, cmd);
            else if (cmd == "list_files")
                handleListFiles(args, clientSocket, currUsr);
            else if (cmd == "download_file")
                handleDownloadFile(args, clientSocket, currUsr);
            else if (cmd == "stop_share" || cmd == "SYNC_STOP_SHARE")
                handleStopShare(args, clientSocket, currUsr, cmd);
            else if (cmd == "logout")
                handleLogout(clientSocket, currUsr);
            else
                sendResponse(clientSocket, "ERROR: Unknown command.\n");
        }
    }
}

static int connectToPeer(const char* ip, int port) {
    int sock = 0;
    struct sockaddr_in servAddr{};
    while (true) {
        if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) { this_thread::sleep_for(chrono::seconds(3)); continue; }
        servAddr.sin_family = AF_INET; servAddr.sin_port = htons(port);
        if (inet_pton(AF_INET, ip, &servAddr.sin_addr) <= 0) { close(sock); this_thread::sleep_for(chrono::seconds(3)); continue; }
        if (connect(sock, (struct sockaddr *)&servAddr, sizeof(servAddr)) < 0) { close(sock); this_thread::sleep_for(chrono::seconds(3)); continue; }
        cout << "[SYNC] Successfully connected to peer tracker at " << ip << ":" << port << endl;
        return sock;
    }
}

int main(int argc, char *argv[]) {
    if (argc < 3) { cerr << "Usage: ./tracker <tracker_info.txt> <tracker_number>\n"; return 1; }
    string tracker_info_file = argv[1]; int tracker_num = stoi(argv[2]);

    ifstream info_file(tracker_info_file);
    if (!info_file.is_open()) { cerr << "ERROR: Could not open " << tracker_info_file << endl; return 1; }

    vector<pair<string,int>> tracker_addrs;
    string line;
    while (getline(info_file, line)) {
        size_t colon_pos = line.find(':');
        if (colon_pos != string::npos) {
            string ip = line.substr(0, colon_pos);
            int port = stoi(line.substr(colon_pos + 1));
            tracker_addrs.push_back({ip, port});
        }
    }
    info_file.close();
    if (tracker_addrs.size() < 2) { cerr << "ERROR: tracker_info.txt must contain at least 2 tracker addresses\n"; return 1; }
    if (tracker_num < 1 || tracker_num > (int)tracker_addrs.size()) { cerr << "ERROR: Invalid tracker number\n"; return 1; }

    int my_port = tracker_addrs[tracker_num - 1].second;
    int peer_idx = (tracker_num == 1) ? 1 : 0;
    string peer_ip = tracker_addrs[peer_idx].first;
    int peer_port = tracker_addrs[peer_idx].second;

    int serverFd;
    struct sockaddr_in address{};
    int opt = 1;
    socklen_t addrlen = sizeof(address);

    if ((serverFd = socket(AF_INET, SOCK_STREAM, 0)) == -1) { perror("socket failed"); exit(EXIT_FAILURE); }
    if (setsockopt(serverFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) { perror("setsockopt"); exit(EXIT_FAILURE); }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(my_port);

    if (bind(serverFd, (struct sockaddr *)&address, sizeof(address)) < 0) { perror("bind failed"); exit(EXIT_FAILURE); }
    if (listen(serverFd, 64) < 0) { perror("listen"); exit(EXIT_FAILURE); }

    cout << "Tracker #" << tracker_num << " listening on port " << my_port << endl;

    thread peer_connector([&]() {
        int sock = connectToPeer(peer_ip.c_str(), peer_port);
        lock_guard<mutex> lock(peerSocketMutex);
        peerTrackerSocket = sock;
    });
    peer_connector.detach();

    while (true) {
        int nSocket = accept(serverFd, (struct sockaddr *)&address, &addrlen);
        if (nSocket < 0) { perror("accept"); continue; }
        thread clientThread(handleClient, nSocket);
        clientThread.detach();
    }
    return 0;
}
