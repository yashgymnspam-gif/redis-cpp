#include<iostream>
#include<sstream>
#include<algorithm>
#include<winsock2.h>
#include<vector>
#include<unordered_map>
#include<chrono>
#include<fstream>
#include<csignal>
#include<thread>
#include<mutex>
#pragma comment(lib,"ws2_32.lib")
using namespace std;

unordered_map<string,string> store;
unordered_map<string,chrono::system_clock::time_point> expiry;
mutex storeMutex;

vector<string> parseRESP(const string& request){
    vector<string> result;
    stringstream ss(request);
    string line;
    while(getline(ss,line)){
        if(line.empty()) continue;
        if(line[0] == '*' || line[0] == '$') continue;
        if(line.back() == '\r') line.pop_back();
        result.push_back(line);
    }
    return result;
}

void handleClient(SOCKET clientSocket){
    while(1){
        char buffer[4097];
        int bytesReceived = recv(clientSocket,buffer,sizeof(buffer),0);
        if(bytesReceived<=0) break;
        buffer[bytesReceived] = '\0';
        string request(buffer);
        vector<string> cmd = parseRESP(request);
        if(cmd.empty()) continue;
        transform(cmd[0].begin(),cmd[0].end(),cmd[0].begin(),::toupper);

        string response;
        {
        lock_guard<mutex> lock(storeMutex);

        if(cmd[0] == "PING"){
            if(cmd.size()==1) response = "+PONG\r\n";
            else              response = "+" + cmd[1] + "\r\n";
        }
        else if(cmd[0] == "SET"){
            if(cmd.size()<3){ response = "-ERR wrong number of arguments for 'SET'\r\n"; }
            else{ store[cmd[1]]=cmd[2]; response = "+OK\r\n"; }
        }
        else if(cmd[0] == "GET"){
            if(cmd.size()<2){ response = "-ERR wrong number of arguments for 'GET'\r\n"; }
            else{
                if(expiry.count(cmd[1]) && chrono::system_clock::now()>expiry[cmd[1]]){
                    store.erase(cmd[1]); expiry.erase(cmd[1]);
                }
                if(store.count(cmd[1]))
                    response = "$" + to_string(store[cmd[1]].size()) + "\r\n" + store[cmd[1]] + "\r\n";
                else
                    response = "$-1\r\n";
            }
        }
        else if(cmd[0] == "DEL"){
            if(cmd.size()<2){ response = "-ERR wrong number of arguments for 'DEL'\r\n"; }
            else{
                int del=0;
                for(int i=1;i<(int)cmd.size();i++){
                    del+=store.erase(cmd[i]);
                    expiry.erase(cmd[i]);
                }
                response = ":" + to_string(del) + "\r\n";
            }
        }
        else if(cmd[0] == "EXISTS"){
            if(cmd.size()<2){ response = "-ERR wrong number of arguments for 'EXISTS'\r\n"; }
            else{
                auto now = chrono::system_clock::now();
                if(expiry.count(cmd[1]) && now>expiry[cmd[1]]){
                    store.erase(cmd[1]); expiry.erase(cmd[1]);
                }
                response = ":" + to_string(store.count(cmd[1])) + "\r\n";
            }
        }
        else if(cmd[0] == "KEYS"){
            vector<string> live;
            auto now = chrono::system_clock::now();
            for(auto& [k,v] : store){
                if(expiry.count(k) && now > expiry[k]) continue;
                live.push_back(k);
            }
            response = "*" + to_string(live.size()) + "\r\n";
            for(auto& k : live)
                response += "$" + to_string(k.size()) + "\r\n" + k + "\r\n";
        }
        else if(cmd[0] == "EXPIRE"){
            if(cmd.size()<3){ response = "-ERR wrong number of arguments for 'EXPIRE'\r\n"; }
            else{
                if(store.count(cmd[1])){
                    expiry[cmd[1]] = chrono::system_clock::now() + chrono::seconds(stoi(cmd[2]));
                    response = ":1\r\n";
                } else {
                    response = ":0\r\n";
                }
            }
        }
        else if(cmd[0] == "TTL"){
            if(cmd.size()<2){ response = "-ERR wrong number of arguments for 'TTL'\r\n"; }
            else{
                if(!store.count(cmd[1])){ response = ":-2\r\n"; }
                else if(!expiry.count(cmd[1])){ response = ":-1\r\n"; }
                else{
                    auto remaining = expiry[cmd[1]] - chrono::system_clock::now();
                    auto remaining_ms = chrono::duration_cast<chrono::milliseconds>(remaining).count();
                    int ttl = (int)((remaining_ms + 999) / 1000);
                    if(ttl < 0){
                        store.erase(cmd[1]); expiry.erase(cmd[1]);
                        response = ":-2\r\n";
                    } else {
                        response = ":" + to_string(ttl) + "\r\n";
                    }
                }
            }
        }
        else if(cmd[0] == "INCR"){
            if(cmd.size()<2){ response = "-ERR wrong number of arguments for 'INCR'\r\n"; }
            else{
                auto now = chrono::system_clock::now();
                if(expiry.count(cmd[1]) && now>expiry[cmd[1]]){
                    store.erase(cmd[1]); expiry.erase(cmd[1]);
                }
                int val=0;
                if(store.count(cmd[1])){
                    try{ val=stoi(store[cmd[1]]); }
                    catch(...){
                        response = "-ERR value is not an integer or out of range\r\n";
                        send(clientSocket,response.c_str(),response.length(),0);
                        continue;
                    }
                }
                val++;
                store[cmd[1]] = to_string(val);
                response = ":" + to_string(val) + "\r\n";
            }
        }
        else if(cmd[0] == "APPEND"){
            if(cmd.size()<3){ response = "-ERR wrong number of arguments for 'APPEND'\r\n"; }
            else{
                auto now = chrono::system_clock::now();
                if(expiry.count(cmd[1]) && now>expiry[cmd[1]]){
                    store.erase(cmd[1]); expiry.erase(cmd[1]);
                }
                store[cmd[1]] += cmd[2];
                response = ":" + to_string(store[cmd[1]].size()) + "\r\n";
            }
        }
        else if(cmd[0] == "MSET"){
            if(cmd.size()<3 || (cmd.size()-1)%2 != 0){
                response = "-ERR wrong number of arguments for 'MSET'\r\n";
            } else {
                for(int i=1;i<(int)cmd.size();i+=2)
                    store[cmd[i]]=cmd[i+1];
                response = "+OK\r\n";
            }
        }
        else{
            response = "-ERR unknown command '" + cmd[0] + "'\r\n";
        }
        }
        send(clientSocket,response.c_str(),response.size(),0);
    }
    closesocket(clientSocket);
}


void save(){
    ofstream file("store.txt");
    for(auto& [k,v]:store){
        long long exp = -1;
        if(expiry.count(k))
            exp = chrono::duration_cast<chrono::seconds>(expiry[k].time_since_epoch()).count();
            file << k << endl << v << endl << exp << endl;
    } 

}

void load(){
    ifstream file("store.txt");
    if(!file) return;
    string k,v;
    long long exp;
    while(getline(file,k) && getline(file,v) && file >> exp){
        file.ignore();
        store[k]=v;
        if(exp!=-1)
            expiry[k]= chrono::system_clock::time_point(chrono::seconds(exp));
    }
}

void handle(int sig){
    save();
    exit(0);
}


int main(){
    WSADATA wsa;
    WSAStartup(MAKEWORD(2,2),&wsa);

    SOCKET server = socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
    if(server == INVALID_SOCKET){
        cout<<"Socket creationj failed!";
        return 1;
    }
    cout<<"Socket created!\n";

    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(6379);
    serverAddr.sin_addr.s_addr = INADDR_ANY;

    bind(server,(sockaddr*)&serverAddr,sizeof(serverAddr));
    listen(server,SOMAXCONN);

    cout<<"Server listening on port 6379\n";
    cout<<"Waiting for Connection...\n";

    signal(SIGINT,handle);
    signal(SIGTERM,handle);
    load();


    while(1){

    sockaddr_in clientAddr;
    int clientSize = sizeof(clientAddr);
    SOCKET clientSocket = accept(server,(sockaddr*)&clientAddr,&clientSize);

    if(clientSocket == INVALID_SOCKET){
        cout<<"Accept Failed";
        continue;
    }
    cout<<"Client Connected\n";

    thread(handleClient,clientSocket).detach();


    }

    closesocket(server);
    WSACleanup();
    return 0;
}