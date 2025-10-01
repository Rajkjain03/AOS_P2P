// client.cpp
// Build: g++ -std=c++17 -pthread client.cpp -o client
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "sha1.h"

using namespace std;

static const int PIECE_SIZE = 512 * 1024;

struct DownloadInfo {
    string status; // "Downloading" or "Completed"
    string group_id;
    string filename;
    int downloaded_pieces = 0;
    int total_pieces = 0;
};

// "group_id:filename" or "group_id:fullpath" -> local_path
static map<string, string> g_local_files;
static mutex g_local_files_mutex;

static map<string, DownloadInfo> g_downloads;
static mutex g_downloads_mutex;

static void error(const char *msg) { perror(msg); exit(1); }

static inline void trim_inplace(string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    size_t b = s.find_last_not_of(" \t\r\n");
    if (a == string::npos) { s.clear(); return; }
    s = s.substr(a, b - a + 1);
}

static string get_sha1_hash(const char* data, size_t size) {
    SHA1 checksum;
    checksum.update(string(data, size));
    return checksum.final();
}

static void handle_peer_connection(int peer_socket) {
    char buffer[1024] = {0};
    int n = read(peer_socket, buffer, 1023);
    if (n <= 0) { close(peer_socket); return; }

    string request(buffer, buffer + n);
    stringstream ss(request);
    string part;
    vector<string> args;
    while (getline(ss, part, ':')) {
        if (!part.empty() && (part.back() == '\n' || part.back() == '\r')) part.pop_back();
        trim_inplace(part);
        args.push_back(part);
    }
    if (args.size() >= 4 && args[0] == "GET_PIECE") {
        string group_id = args[1];
        string filename = args[2];
        int piece_index = stoi(args[3]);
        string key = group_id + ":" + filename;
        string file_path;

        {
            lock_guard<mutex> lock(g_local_files_mutex);
            auto it = g_local_files.find(key);
            if (it != g_local_files.end()) {
                file_path = it->second;
            } else {
                cout << "[Peer Server] No mapping for key=" << key << endl;
                cout << "[Peer Server] Available mappings:" << endl;
                for (auto const& kv : g_local_files) {
                    cout << "  " << kv.first << " -> " << kv.second << endl;
                }
            }
        }

        if (file_path.empty()) {
            close(peer_socket);
            return;
        }

        FILE* fp = fopen(file_path.c_str(), "rb");
        if (!fp) { cout << "[Peer Server] ERROR: Could not open file " << file_path << endl; close(peer_socket); return; }

        fseeko(fp, static_cast<off_t>(piece_index) * PIECE_SIZE, SEEK_SET);
        vector<char> piece_buffer(PIECE_SIZE);
        size_t bytes_read = fread(piece_buffer.data(), 1, PIECE_SIZE, fp);
        fclose(fp);

        if (bytes_read > 0) {
            size_t sent = 0;
            while (sent < bytes_read) {
                ssize_t w = write(peer_socket, piece_buffer.data() + sent, bytes_read - sent);
                if (w <= 0) break;
                sent += static_cast<size_t>(w);
            }
        }
    }
    close(peer_socket);
}

static void peer_server_thread(int port) {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) { perror("socket"); exit(EXIT_FAILURE); }
    struct sockaddr_in serv_addr{};
    int opt = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) { perror("setsockopt"); exit(EXIT_FAILURE); }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(port);

    if (bind(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        cout << "FATAL: Could not bind to port " << port << ". It may be in use." << endl;
        exit(EXIT_FAILURE);
    }
    if (listen(sockfd, 64) < 0) { perror("listen"); exit(EXIT_FAILURE); }
    cout << "[Peer Server] Listening on port " << port << endl;

    while (true) {
        int newsockfd = accept(sockfd, nullptr, nullptr);
        if (newsockfd >= 0) thread(handle_peer_connection, newsockfd).detach();
    }
}

// Normalize seeders from tokens possibly containing "IP" and "PORT" split by spaces
static vector<string> normalize_seeders_from_tokens(const vector<string>& tokens) {
    vector<string> out;
    for (size_t i = 0; i < tokens.size();) {
        string t = tokens[i];
        string tnext = (i + 1 < tokens.size() ? tokens[i + 1] : "");
        // already ip:port
        if (t.find(':') != string::npos) {
            string addr = t;
            trim_inplace(addr);
            out.push_back(addr);
            i += 1;
            continue;
        }
        // if t looks like IP and next looks like PORT (digits), combine
        bool next_is_port = !tnext.empty() && tnext.find_first_not_of("0123456789") == string::npos;
        bool looks_ip = (t.find('.') != string::npos || t == "localhost");
        if (looks_ip && next_is_port) {
            string addr = t + ":" + tnext;
            trim_inplace(addr);
            out.push_back(addr);
            i += 2;
            continue;
        }
        // fallback: skip malformed
        i += 1;
    }
    return out;
}

// This thread only downloads from peers. Seeder info is prepared in main on the logged-in socket.
static void download_pieces_thread(long long file_size, string combined_hashes, vector<string> seeders,
                                   string group_id, string filename, string dest_path) {
    cout << "Download thread started for " << filename << endl;

    string download_key = group_id + ":" + filename;
    int num_pieces = static_cast<int>((file_size + PIECE_SIZE - 1) / PIECE_SIZE);
    {
        lock_guard<mutex> lock(g_downloads_mutex);
        g_downloads[download_key] = {"Downloading", group_id, filename, 0, num_pieces};
    }

    cout << "[Downloader] file_size=" << file_size
         << " num_pieces=" << num_pieces
         << " combined_hashes_len=" << combined_hashes.size()
         << " seeders=";
    for (auto &s : seeders) cout << s << " ";
    cout << endl;

    if (file_size <= 0) { cerr << "[Downloader] ERROR: file_size <= 0\n"; return; }
    if (combined_hashes.size() != static_cast<size_t>(num_pieces) * 40) {
        cerr << "[Downloader] ERROR: combined_hashes length mismatch. Expected "
             << (num_pieces * 40) << " got " << combined_hashes.size() << endl;
        return;
    }
    if (seeders.empty()) { cerr << "[Downloader] ERROR: No seeders available\n"; return; }

    FILE* fp = fopen(dest_path.c_str(), "wb+");
    if (!fp) { perror("Failed to open destination file"); return; }
    if (file_size > 0) {
        if (fseeko(fp, file_size - 1, SEEK_SET) == 0) fputc('\0', fp);
        fflush(fp);
    }

    for (int i = 0; i < num_pieces; ++i) {
        bool piece_success = false;
        int retry_count = 0;
        while (!piece_success && retry_count < static_cast<int>(seeders.size()) * 3) {
            string seeder_addr = seeders[retry_count % seeders.size()];
            trim_inplace(seeder_addr);
            size_t colon_pos = seeder_addr.find(':');
            if (colon_pos == string::npos) {
                cout << "Bad seeder addr: '" << seeder_addr << "'\n";
                retry_count++;
                continue;
            }

            string ip = seeder_addr.substr(0, colon_pos);
            string port_str = seeder_addr.substr(colon_pos + 1);
            trim_inplace(ip);
            trim_inplace(port_str);
            int port = 0;
            try { port = stoi(port_str); } catch (...) { cout << "Bad port in addr: '" << seeder_addr << "'\n"; retry_count++; continue; }

            int peer_sock = socket(AF_INET, SOCK_STREAM, 0);
            if (peer_sock < 0) { retry_count++; continue; }
            struct sockaddr_in peer_serv_addr{};
            peer_serv_addr.sin_family = AF_INET;
            peer_serv_addr.sin_port = htons(port);
            if (ip == "localhost") ip = "127.0.0.1";
            if (inet_pton(AF_INET, ip.c_str(), &peer_serv_addr.sin_addr) <= 0) {
                cout << "Invalid IP for seeder '" << ip << "'\n";
                close(peer_sock);
                retry_count++;
                continue;
            }

            if (connect(peer_sock, (struct sockaddr *)&peer_serv_addr, sizeof(peer_serv_addr)) < 0) {
                cout << "Failed to connect to peer " << seeder_addr << " (errno=" << errno << ")\n";
                close(peer_sock);
                retry_count++;
                this_thread::sleep_for(chrono::milliseconds(200));
                continue;
            }

            string piece_req = "GET_PIECE:" + group_id + ":" + filename + ":" + to_string(i) + "\n";
            if (write(peer_sock, piece_req.c_str(), piece_req.length()) <= 0) {
                close(peer_sock);
                retry_count++;
                continue;
            }

            size_t expected_size_this_piece = static_cast<size_t>(min(
                static_cast<long long>(PIECE_SIZE),
                file_size - (static_cast<long long>(i) * PIECE_SIZE)));

            vector<char> piece_buffer(expected_size_this_piece);
            size_t total_bytes_read = 0;
            while (total_bytes_read < expected_size_this_piece) {
                int bytes_read = read(peer_sock, piece_buffer.data() + total_bytes_read,
                                      static_cast<int>(expected_size_this_piece - total_bytes_read));
                if (bytes_read <= 0) break;
                total_bytes_read += static_cast<size_t>(bytes_read);
            }
            close(peer_sock);

            if (total_bytes_read != expected_size_this_piece) {
                cout << "Incomplete piece " << i << " received (" << total_bytes_read
                     << "/" << expected_size_this_piece << "). Retrying..." << endl;
                retry_count++;
                continue;
            }

            string received_hash = get_sha1_hash(piece_buffer.data(), total_bytes_read);
            string expected_hash = combined_hashes.substr(static_cast<size_t>(i) * 40, 40);
            if (received_hash == expected_hash) {
                fseeko(fp, static_cast<off_t>(i) * PIECE_SIZE, SEEK_SET);
                fwrite(piece_buffer.data(), 1, total_bytes_read, fp);
                piece_success = true;

                lock_guard<mutex> lock(g_downloads_mutex);
                g_downloads[download_key].downloaded_pieces = i + 1;
            } else {
                cout << "Piece " << i << " hash mismatch. Retrying..." << endl;
                retry_count++;
            }
        }
        if (!piece_success) {
            cout << "Failed to download piece " << i << " after multiple retries" << endl;
            fclose(fp);
            return;
        }
    }
    fclose(fp);

    {
        lock_guard<mutex> lock(g_local_files_mutex);
        g_local_files[download_key] = dest_path;
    }
    {
        lock_guard<mutex> lock(g_downloads_mutex);
        g_downloads[download_key].status = "Completed";
    }
    cout << "\nDownload of " << filename << " completed." << endl;
}

// Fetch seeder info on the logged-in socket; then launch peer-only thread
static bool fetch_seeder_info_and_spawn(int sockfd, const string& group_id, const string& filename, const string& dest_path) {
    string cmd = "download_file:" + group_id + ":" + filename + "\n";
    if (write(sockfd, cmd.c_str(), cmd.size()) <= 0) {
        cout << "ERROR: write to tracker failed\n";
        return false;
    }
    vector<char> buf(65536, 0);
    int r = read(sockfd, buf.data(), static_cast<int>(buf.size()) - 1);
    if (r <= 0) {
        cout << "ERROR: Empty response from tracker\n";
        return false;
    }
    string response(buf.data(), buf.data() + r);
    if (response.rfind("ERROR", 0) == 0) {
        cout << "SERVER: " << response;
        return false;
    }

    // Parse "file_size:combined_hashes:seeder_tokens..."
    stringstream ss(response);
    string part;
    if (!getline(ss, part, ':')) { cout << "Bad tracker response\n"; return false; }
    trim_inplace(part);
    long long file_size = 0;
    try { file_size = stoll(part); } catch (...) { cout << "Bad file size in response\n"; return false; }

    if (!getline(ss, part, ':')) { cout << "Bad tracker response (missing hashes)\n"; return false; }
    trim_inplace(part);
    string combined_hashes = part;

    vector<string> tokens;
    while (getline(ss, part, ':')) {
        trim_inplace(part);
        if (!part.empty()) tokens.push_back(part);
    }
    // Normalize seeders (handle "ip port" and "ip:port")
    vector<string> seeders = normalize_seeders_from_tokens(tokens);

    int num_pieces = static_cast<int>((file_size + PIECE_SIZE - 1) / PIECE_SIZE);
    cout << "[SeederInfo] file_size=" << file_size << " pieces=" << num_pieces
         << " hashes_len=" << combined_hashes.size() << " seeders=";
    for (auto& s : seeders) cout << s << " ";
    cout << endl;

    if (seeders.empty()) {
        cout << "No seeders available for this file.\n";
        return false;
    }

    thread(download_pieces_thread, file_size, combined_hashes, seeders, group_id, filename, dest_path).detach();
    return true;
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr,"Usage: %s <tracker_ip:port> <client_listen_port>\n", argv[0]);
        exit(1);
    }

    string tracker_addr(argv[1]);
    size_t colon_pos = tracker_addr.find(':');
    if (colon_pos == string::npos) {
        fprintf(stderr, "Invalid tracker address format. Use IP:PORT\n");
        exit(1);
    }
    string tracker_ip = tracker_addr.substr(0, colon_pos);
    int tracker_port = stoi(tracker_addr.substr(colon_pos + 1));
    int client_listen_port = atoi(argv[2]);

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) error("socket");
    struct sockaddr_in serv_addr{};
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(tracker_port);
    inet_pton(AF_INET, tracker_ip.c_str(), &serv_addr.sin_addr);
    if (connect(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
        error("ERROR connecting to tracker");
    }

    thread(peer_server_thread, client_listen_port).detach();
    cout << "Connected to tracker. Enter commands." << endl;

    // Command loop
    for (string line; getline(cin, line);) {
        if (line == "quit") break;

        stringstream ss(line);
        string command;
        ss >> command;
        vector<string> parts;
        parts.push_back(command);
        {
            string p;
            while (ss >> p) parts.push_back(p);
        }

        if (command == "upload_file") {
            if (parts.size() != 3) {
                cout << "Usage: upload_file <group_id> <file_path>" << endl;
                continue;
            }
            string group_id = parts[1], file_path = parts[2];

            ifstream file_stream(file_path, ifstream::binary);
            if (!file_stream) {
                cout << "ERROR: Cannot open file " << file_path << endl;
                continue;
            }
            file_stream.seekg(0, file_stream.end);
            long long file_size = file_stream.tellg();
            file_stream.seekg(0, file_stream.beg);

            string combined_hashes;
            vector<char> piece_buffer(PIECE_SIZE);
            while (file_stream) {
                file_stream.read(piece_buffer.data(), PIECE_SIZE);
                streamsize g = file_stream.gcount();
                if (g > 0) combined_hashes += get_sha1_hash(piece_buffer.data(), static_cast<size_t>(g));
            }
            file_stream.close();

            // Store local file path for seeding (both full path and basename)
            {
                lock_guard<mutex> lock(g_local_files_mutex);
                string key_full = group_id + ":" + file_path;
                g_local_files[key_full] = file_path;
                size_t p2 = file_path.find_last_of("/\\");
                string base = (p2 == string::npos) ? file_path : file_path.substr(p2 + 1);
                string key_base = group_id + ":" + base;
                g_local_files[key_base] = file_path;
            }

            string cmd_str = "upload_file:" + group_id + ":" + file_path + ":" +
                             to_string(file_size) + ":" + to_string(client_listen_port) + ":" +
                             combined_hashes + "\n";

            if (write(sockfd, cmd_str.c_str(), cmd_str.length()) <= 0) {
                cout << "ERROR: write to tracker failed\n";
                continue;
            }
            char buffer[4096] = {0};
            int r = read(sockfd, buffer, 4095);
            if (r > 0) cout << "SERVER: " << string(buffer, buffer + r);
            else cout << "SERVER: (no response)\n";
        } else if (command == "download_file") {
            if (parts.size() != 4) {
                cout << "Usage: download_file <group_id> <filename> <dest_path>" << endl;
                continue;
            }
            string group_id = parts[1], filename = parts[2], dest_path = parts[3];
            if (!fetch_seeder_info_and_spawn(sockfd, group_id, filename, dest_path)) {
                cout << "Download could not be started.\n";
            }
            continue;
        } else if (command == "show_downloads") {
            lock_guard<mutex> lock(g_downloads_mutex);
            if (g_downloads.empty()) {
                cout << "No active or completed downloads." << endl;
            } else {
                for (auto const& [key, val] : g_downloads) {
                    cout << "[" << (val.status == "Completed" ? "C" : "D") << "] "
                         << "[" << val.group_id << "] " << val.filename;
                    if (val.status == "Downloading") {
                        cout << " (" << val.downloaded_pieces << "/" << val.total_pieces << " pieces)";
                    }
                    cout << endl;
                }
            }
            continue;
        } else {
            // Forward any other command to tracker: convert spaces->colons for user input
            string formatted = line;
            replace(formatted.begin(), formatted.end(), ' ', ':');
            formatted += "\n";
            if (write(sockfd, formatted.c_str(), formatted.length()) <= 0) {
                cout << "ERROR: write to tracker failed\n";
                continue;
            }
            char buffer[4096] = {0};
            int r = read(sockfd, buffer, 4095);
            if (r > 0) cout << "SERVER: " << string(buffer, buffer + r);
            else cout << "SERVER: (no response)\n";
        }
    }

    close(sockfd);
    return 0;
}
