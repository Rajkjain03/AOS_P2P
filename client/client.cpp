#include <iostream> // for cout,cin
#include <string>   // for string
#include <vector> // for vector
#include <sstream> // for stringstream
#include <cstring> // for memset, memcpy
#include <unistd.h> // for close
#include <sys/socket.h> // for socket functions
#include <netinet/in.h> // for sockaddr_in
#include <arpa/inet.h> // for inet_pton
#include <netdb.h> // for gethostbyname

using namespace std;

// to print error and exit
void error(const char *msg) {
    perror(msg);
    exit(1);
}

// function to connect to the tracker
int connect_to_tracker(const char* ip, int port) {
    // socket creation 
    int sockfd;
    // struct to hold server address
    struct sockaddr_in serv_addr;

    // Create socket
    // AF_INET - IPv4, SOCK_STREAM - TCP
    // AF_INET - The address family for IPv4
    // SOCK_STREAM - Provides sequenced, reliable, two-way, connection-based byte streams.
    // 0 - Default protocol (TCP for SOCK_STREAM)
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) 
        error("ERROR ->  opening socket");

    // Set server address struct
    // bzero - sets all bytes to zero
    // sizeof(serv_addr) - size of the struct
    // sin_family - address family (AF_INET for IPv4)
    // sin_port - port number (htons converts to network byte order)
    // inet_pton - converts IP address from text to binary form
    bzero((char *) &serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    
    if(inet_pton(AF_INET, ip, &serv_addr.sin_addr) <= 0) {
        error("Invalid address/ Address not supported");
    }

    if (connect(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) 
        error("ERROR ->  connecting");
        
    return sockfd;
}

// replaces spaces with colons and handles extra whitespace
string format_command(const string& input) {
    stringstream ss(input);
    string sgmnt;
    string formattedCmnd;

    // Use the extraction operator '>>' which automatically handles whitespace
    // >> this operator reads until the next whitespace
    if (ss >> sgmnt) {
        formattedCmnd += sgmnt;
    }

    while (ss >> sgmnt) {
        formattedCmnd += ":" + sgmnt;
    }

    return formattedCmnd + "\n";
}

int main(int argc, char *argv[]) {
    // check correct number of arguments
    if (argc < 3) {
       cout << "Usage: " << argv[0] << " <tracker_ip> <tracker_port>" << endl;
       exit(1);
    }

    // get IP and port from command line arguments
    const char* ip = argv[1];
    int port = atoi(argv[2]);

    // connect to tracker
    int sockfd = connect_to_tracker(ip, port);

    cout << "Success: Connected to tracker" << endl;

    // getline - reads a line from input stream (cin) into string (line)
    // loop to read commands from user
    for (string line; getline(cin, line);) {
        if (line == "quit") {
            break;
        }

        // Format command by replacing spaces with colons
        string command_to_send = format_command(line);

        // Send command to tracker
        // write - writes data to the socket
        if (write(sockfd, command_to_send.c_str(), command_to_send.length()) < 0) 
             error("ERROR ->  writing to socket");

        // Read response from tracker
        // buffer to hold response  
        char buffer[2048] = {0};
        if (read(sockfd, buffer, 2047) < 0) 
             error("ERROR ->  reading from socket");

        cout << "SERVER: " << buffer;
    }

    close(sockfd);
    return 0;
}