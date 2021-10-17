#include <bits/stdc++.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <filesystem>
#include <sys/stat.h>
#include <poll.h>

#include <pthread.h>

using namespace std;

#include <chrono>
using namespace std::chrono;

#define PORT 8080


const string WHITESPACE = " \n\r\t\f\v";
string ltrim(const string &s)
{
    size_t start = s.find_first_not_of(WHITESPACE);
    return (start == string::npos) ? "" : s.substr(start);
}
 
string rtrim(const string &s)
{
    size_t end = s.find_last_not_of(WHITESPACE);
    return (end == string::npos) ? "" : s.substr(0, end + 1);
}
 
string trim(const string &s) {
    return rtrim(ltrim(s));
}   

int isDirectory(const string &path) {
    struct stat statbuf;
    if (stat(path.c_str(), &statbuf) != 0)
       return 0;
    return S_ISDIR(statbuf.st_mode);
}

int isFileExists(const string& name) {
    struct stat buffer;   
    return (stat (name.c_str(), &buffer) == 0); 
}

string readFile(const string& filename) {
    ifstream f(filename); //taking file as inputstream
    string str = "";
    if(f) {
        ostringstream ss;
        ss << f.rdbuf(); // reading data
        str = ss.str();
    }
    return str;
}


class HTTPRequest {
    /*
    Parser for HTTP requests. 
    It takes raw data and extracts meaningful information about the incoming request.
    Instances of this class have the following attributes:
        this->method: The current HTTP request method sent by client (string)
        this->uri: URI for the current request (string)
        this->http_version = HTTP version used by  the client (string)
        this->rem_body = the remaining body of the HTTP request
    */

public:
    string method, uri, http_version;
    vector<string> rem_body;

    HTTPRequest(string &data) {
        this->method = "";
        this->uri = "";
        this->http_version = "1.1";
        this->rem_body.clear();
        parse(data);
    }

    vector<string> split_string(string& str, string delimiter) {
        vector<string> strings;
        string::size_type pos = 0;
        string::size_type prev = 0;

        while ((pos = str.find(delimiter, prev)) != string::npos) {
            strings.push_back(str.substr(prev, pos - prev));
            prev = pos + 2;
        }

        strings.pop_back(); 
        return strings;
    }

    vector<string> split_line(string &line) {
        stringstream s(line);
        vector<string> words;
        string word;
        while(s >> word) {
            words.push_back(word);
        }
        return words;
    }

    void parse(string &data) {
        vector<string> lines = split_string(data, "\r\n");
        string req_line = lines[0];

        vector<string> words = split_line(req_line);
        this->method = words[0];
        if (words.size() > 1) {
            this->uri = words[1].substr(1); // remove the starting '/'
        }
        if (words.size() > 2) {
            this->http_version = words[2];
        }
        this->rem_body = lines;
    }
};


class TCPServer {
public:
    string host;
    int port;
    int server_fd, new_socket; 
    long valread;
    struct sockaddr_in address;
    
    // pthread part
    vector<pollfd> readfds;
    vector<int> clients;

    TCPServer() {}

    TCPServer(string host, int port) {
        this->port = port;
        this->host = host;
    }

    void start(int tid) {
        socklen_t addrlen = sizeof(address);
        if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
            perror("In socket");
            exit(EXIT_FAILURE);
        }

        int opt = 1;
        if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
            perror("setsockopt");
            exit(EXIT_FAILURE);
        }

        address.sin_family = AF_INET;
        address.sin_addr.s_addr = INADDR_ANY;
        address.sin_port = htons( PORT );

        memset(address.sin_zero, '\0', sizeof address.sin_zero);
    
        if (bind(server_fd, (struct sockaddr *)&address, sizeof(address))<0) {
            perror("In bind");
            exit(EXIT_FAILURE);
        }
        if (listen(server_fd, 10) < 0) {
            perror("In listen");
            exit(EXIT_FAILURE);
        }

        readfds.clear();
        clients.clear();
        
        char buffer[4000] = {0};
        pollfd pfd;

        //add master socket to set
        pfd.fd = server_fd;
        pfd.events = POLLIN;
        pfd.revents = 0;
        readfds.push_back(pfd);

        while (1) {

            // Wait indefinitely for an activity on one of the sockets
            int activity = poll(&readfds[0], readfds.size(), -1);
            if (activity < 0) {
                if (errno == EINTR) continue;
                perror("poll() failed");
                exit(EXIT_FAILURE);
            }

            // Handle incoming connections, client disconnections, and incoming data
            size_t j = 0;
            while (j < readfds.size()) {
                if (readfds[j].revents == 0) {
                    ++j;
                    continue;
                }

                int sd = readfds[j].fd;

                if (readfds[j].revents & POLLIN) {
                    if (sd == server_fd) {
                        new_socket = accept(server_fd, (struct sockaddr *) &address, &addrlen);
                        if (new_socket < 0) {
                            perror("In accept");
                            exit(EXIT_FAILURE);
                        }

                        // cout    << "New connection on tid=" << tid << " : "
                        //         << "[SOCKET_FD : " << new_socket
                        //         << " , IP : " << inet_ntoa(address.sin_addr)
                        //         << " , PORT : " << ntohs(address.sin_port)
                        //         << "]" << endl;
                        

                        // Add connection to vectors
                        clients.push_back(new_socket);    
                        pfd.fd = new_socket;
                        pfd.events = POLLIN | POLLRDHUP;
                        pfd.revents = 0;
                        readfds.push_back(pfd);
                    }
                    else {
                        int rc = read(sd, buffer, sizeof(buffer));
                        if (rc > 0) {
                            string input;
                            input.assign(buffer, rc);

                            if (input.substr(0, 4) == "POST") {
                                // handle post, as it sends two messages for single event
                                rc = read(sd, buffer, sizeof(buffer));
                                input.assign(buffer, rc);
                                input = "POST " + input;
                                string response = handle_request(input);
                                write(sd , response.c_str(), response.size());
                            }
                            else {
                                string response = handle_request(input);
                                write(sd , response.c_str(), response.size());
                            }
                        }
                        else if (rc == 0) {
                            readfds[j].revents |= POLLHUP;
                        } 
                        else {
                            readfds[j].revents |= POLLERR;
                        }
                    }
                }

                if(readfds[j].revents != POLLIN && sd != server_fd) {
                    // string str = (readfds[j].revents & POLLERR) ? "read error" : "disconnected";
                    // cout << "On tid=" << tid << ", Client " << str << "! [SOCKET_FD: " << sd << "]" << endl;
                    close(sd);
                    clients.erase(find(clients.begin(), clients.end(), sd));
                    readfds.erase(readfds.begin() + j);
                    continue;
                }

                ++j;
            }
        }
        
    }

    virtual string handle_request(string &input) {
        /* Handles incoming data and returns a response.
        Override this in subclass. */
        return input;
    }

};

class HTTPServer : public TCPServer {
public:
    unordered_map<string, string> mime;
    unordered_map<int, string> status_codes;

    HTTPServer() {}

    HTTPServer(string host, int port): TCPServer(host, port)
    {
        initHashMaps();
    }
    void initHashMaps() {
        status_codes[200] = "OK";
        status_codes[404] = "Not Found";
        status_codes[501] = "Not Implemented";

        mime[".html"] = "text/html";
        mime[".avi"] = "video/x-msvideo";
        mime[".bmp"] = "image/bmp";
        mime[".c"] = "text/plain";
        mime[".doc"] = "application/msword";
        mime[".gif"] = "image/gif";
        mime[".gz"] = "application/x-gzip";
        mime[".htm"] = "text/html";
        mime[".ico"] = "application/x-ico";
        mime[".jpg"] = "image/jpeg";
        mime[".png"] = "image/png";
        mime[".txt"] = "text/plain";
        mime[".mp3"] = "audio/mp3";
        mime["default"] = "text/html";
    }

    string handle_request(string &data) {

        // for testing clients.cpp
        if (data.substr(0, 3) == "TID") {
            return data;
        }

        HTTPRequest *request = new HTTPRequest(data);
        string response = "";

        if (request->method == "GET") {
            response = handle_GET(request);
        }
        else if (request->method == "POST") {
            response = handle_POST(request);
        }
        else {
            response = HTTP_501_handler(request);
        }
        return response;
    }

    string response_line(int code) {
        string reason = status_codes[code];
        string line = "HTTP/1.1 " + to_string(code) + " " + reason +  "\r\n";
        return line;
    }

    string isImage(const string &path) {
        if (path.find(".jpg") != string::npos) {
            return ".jpg";
        }
        else if (path.find(".jpeg") != string::npos) {
            return ".jpeg";
        }
        else if (path.find(".png") != string::npos) {
            return ".png";
        }
        return "-1";
    }

    vector<string> parseForm(string &data) {
        data = data.substr(10);
        string name = "";
        int i;
        for(i=0; i<data.size(); i++) {
            if (data[i]=='&') {
                break;
            }
            name += data[i];
        }
        i+=5;
        string age = "";
        for(i; i<data.size(); i++) {
            age += data[i];
        }
        vector<string> res = {name, age};
        return res;
    }

    string handle_GET(HTTPRequest* &request) {
        string path = trim(request->uri);
        string res_line = "", res_body = "", res_headers = "";

        string tag = ".html";

        if (path.size() == 0) {
            path = "home.html";
        }
        if (isFileExists(path) && !isDirectory(path)) {
            res_line = response_line(200);
            res_body = readFile(path);
            
            string s = isImage(path);
            if (s != "-1") {
                tag = s;
            }
            res_headers = "Content-Type: " + mime[tag] + "\r\n";
            res_headers += "Content-Length: " + to_string(res_body.size()) + "\r\n";
        }
        else {
            res_line = response_line(404);
            res_body = "<h1>404 Not Found</h1>";
            res_headers = "Content-Type: " + mime[tag] + "\r\n";
            res_headers += "Content-Length: " + to_string(res_body.size()) + "\r\n";
        }

        string blank_line = "\r\n";

        string response = res_line + res_headers + blank_line + res_body;
        return response;
    }

    string handle_POST(HTTPRequest* &request) {
        string cont_type = "Content-Type: text/plain";
        string path;

        for(int i=0; i<request->rem_body.size(); i++) {
            if (request->rem_body[i].substr(0, 19) != "Content-Disposition") {
                continue;
            }
            if (request->rem_body[i].find("name=\"file1\"") != string::npos) {
                cont_type = request->rem_body[i+1];
                int pos = request->rem_body[i].find("filename=");
                pos += 10;

                for(pos; request->rem_body[i][pos] != '\"'; pos++) {
                    path += request->rem_body[i][pos];
                }
                break;
            }
        }
        
        string res_line, res_body, res_headers;
        res_line = response_line(200);
        res_body = readFile(path);
            
        res_headers = cont_type + "\r\n";
        res_headers += "Content-Length: " + to_string(res_body.size()) + "\r\n";

        string blank_line = "\r\n";

        string response = res_line + res_headers + blank_line + res_body;
        return response;
    }

    string HTTP_501_handler(HTTPRequest *request) {
        string res_line = response_line(501);
        string res_body = "<h1>501 Not Implemented</h1>";

        string tag = ".html";
        string res_headers = "Content-Type: " + mime[tag] + "\r\n";
        res_headers += "Content-Length: " + to_string(res_body.size()) + "\r\n";
        string blank_line = "\r\n";
        

        string response = res_line + res_headers + blank_line + res_body;
        return response;
    }
};

void* server_thread(void *args) {
    int tid = *(int*)args;
    HTTPServer *server = new HTTPServer("locahost", PORT);
    server->start(tid);
    pthread_exit(NULL);
}

int main() {
    
    // HTTPServer *server = new HTTPServer("locahost", PORT);
    // server->start(1);
    int lim = 1000;
    vector<pthread_t> threads(lim);
    vector<int> servers(lim);
    
    for(int i=0; i<lim; i++) {
        servers[i] = i+1;
        pthread_create(&threads[i], NULL, server_thread, (void*)&servers[i]);
        // sleep(2);
    }
    
    for(int i=0; i<lim; i++) {
        pthread_join(threads[i], NULL);
    }

    return 0;
}