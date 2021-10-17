#include <bits/stdc++.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <filesystem>
#include <sys/stat.h>
#include <poll.h>

#include <pthread.h>

#include <chrono>
using namespace std::chrono;

using namespace std;

#define PORT 8080

pthread_mutex_t mutx = PTHREAD_MUTEX_INITIALIZER;

int total;

void* client_thread(void* args) {
    int tid = *((int*)args);
    string client_request = "TID: " + to_string(tid);

    struct sockaddr_in server_address;
    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = INADDR_ANY;
    server_address.sin_port = htons(PORT);
 
    int network_socket, connection_status;
    
    bool sent = 0, receive = 0, isConnected = 0;
    int rc, wc;
    char buf[2000] = {0};

    while(!isConnected) {
        network_socket = socket(AF_INET, SOCK_STREAM, 0);
        connection_status = connect(network_socket, (struct sockaddr*)&server_address, sizeof(server_address));
        if (connection_status < 0) {
            continue;
        }
        isConnected = 1;
    }

    int cnt = 0, corr = 0;
    int num = 10;

    while(cnt < num) {

        sent = 0, receive = 0;
        while(!sent || !receive) {

            sent = 0, receive = 0;
            while(!sent) {
                wc = write(network_socket, client_request.c_str(), client_request.size());
                if (wc < 0) {
                    isConnected = 0;
                    network_socket = socket(AF_INET, SOCK_STREAM, 0);
                    while (!isConnected) {
                        connection_status = connect(network_socket, (struct sockaddr*)&server_address, sizeof(server_address));
                        if (connection_status < 0) {
                            continue;
                        }
                        isConnected = 1;
                        cout << "Reconnected_send\n";
                    }
                    continue;
                }
                sent = 1;
            }

            // pthread_mutex_lock(&mutx);
            // cout << "Client[SOCKET_FD: " << to_string(network_socket) << ", TID: " << to_string(tid) << "] sent " << "\n";
            // pthread_mutex_unlock(&mutx);

            while(!receive) {
                rc = read(network_socket, buf, sizeof(buf));
                if (rc <= 0) {
                    isConnected = 0;
                    network_socket = socket(AF_INET, SOCK_STREAM, 0);
                    while (!isConnected) {
                        connection_status = connect(network_socket, (struct sockaddr*)&server_address, sizeof(server_address));
                        if (connection_status < 0) {
                            continue;
                        }
                        isConnected = 1;
                        cout << "Reconnected_receive\n";
                    }
                    break;
                }

                // pthread_mutex_lock(&mutx);
                // cout << "Client[SOCKET_FD: " << to_string(network_socket) << ", TID: " << to_string(tid) << "] received " << "\n";
                // pthread_mutex_unlock(&mutx);

                receive = 1;
                string response;
                response.assign(buf, rc);
                if (response == client_request) {
                    corr++;
                }
            }
        }

        cnt += 1;
    }

    if (num == corr) {
        pthread_mutex_lock(&mutx);
        // cout << "tid=" << tid << ": not ok\n";
        total++;
        pthread_mutex_unlock(&mutx);
    }
    
    close(network_socket);
    pthread_exit(NULL);
 
    return 0;
}
 
int main() {

    total = 0;
    int lim = 10000;
    vector<pthread_t> threads(lim);
    vector<int> clients(lim);
    
    auto start = high_resolution_clock::now();

    for(int i=0; i<lim; i++) {
        clients[i] = i+1;
        pthread_create(&threads[i], NULL, client_thread, (void*)&clients[i]);
        // sleep(2);
    }
    
    for(int i=0; i<lim; i++) {
        pthread_join(threads[i], NULL);
    }

    auto stop = high_resolution_clock::now();
    auto duration = duration_cast<microseconds>(stop - start);
  
    cout << "Total Successful Threads: " << total << "\n";
    cout << "Total Requests sent and received: " << 10*total << "\n";
    cout << "Time (sec): " << (double)duration.count()/ (double)pow(10,6) << endl;

    threads.clear();
    clients.clear();
    return 0;
}
