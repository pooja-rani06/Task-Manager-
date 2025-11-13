// task_server.cpp
#include <iostream>
#include <vector>
#include <unordered_map>
#include <queue>
#include <stack>
#include <string>
#include <algorithm>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <limits>
#include <fstream>
#include <cstring>
#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib") // Visual Studio

using namespace std;

// -------------------- Task struct & TaskManager --------------------
struct Task {
    int id;
    string name;
    string description;
    int priority;
    time_t deadline;
    int duration;
    bool isRecurring;
    int recurringDays;
    bool completed;
    time_t createdAt;

    Task(int _id, string _name, string _desc, int _priority, time_t _deadline,
         int _duration, bool _isRecurring = false, int _recurringDays = 0) {
        id = _id;
        name = _name;
        description = _desc;
        priority = _priority;
        deadline = _deadline;
        duration = _duration;
        isRecurring = _isRecurring;
        recurringDays = _recurringDays;
        completed = false;
        createdAt = time(0);
    }

private:
    string escapeJSON(const string& s) const {
        string result;
        for (char c : s) {
            switch (c) {
                case '"': result += "\\\""; break;
                case '\\': result += "\\\\"; break;
                case '\n': result += "\\n"; break;
                case '\r': result += "\\r"; break;
                case '\t': result += "\\t"; break;
                default: result += c;
            }
        }
        return result;
    }

public:
    string toJSON() const {
        stringstream ss;
        ss << "{";
        ss << "\"id\":" << id << ",";
        ss << "\"name\":\"" << escapeJSON(name) << "\",";
        ss << "\"description\":\"" << escapeJSON(description) << "\",";
        ss << "\"priority\":" << priority << ",";
        ss << "\"deadline\":" << deadline << ",";
        ss << "\"duration\":" << duration << ",";
        ss << "\"isRecurring\":" << (isRecurring ? "true" : "false") << ",";
        ss << "\"recurringDays\":" << recurringDays << ",";
        ss << "\"completed\":" << (completed ? "true" : "false") << ",";
        ss << "\"createdAt\":" << createdAt;
        ss << "}";
        return ss.str();
    }
};

struct ComparePriority {
    bool operator()(Task* t1, Task* t2) {
        // smaller priority number = higher priority
        if (t1->priority == t2->priority) {
            return t1->deadline > t2->deadline;
        }
        return t1->priority > t2->priority;
    }
};

class TaskManager {
private:
    priority_queue<Task*, vector<Task*>, ComparePriority> taskHeap;
    unordered_map<int, Task*> taskMap;
    vector<Task*> allTasks;
    stack<pair<string, Task*>> undoStack;
    stack<pair<string, Task*>> redoStack;
    queue<Task*> recurringQueue;
    vector<Task*> allocatedTasks; // all allocated tasks (to delete at shutdown)
    int nextId;

    void rebuildHeap() {
        priority_queue<Task*, vector<Task*>, ComparePriority> newHeap;
        for (auto task : allTasks) {
            if (!task->completed) {
                newHeap.push(task);
            }
        }
        taskHeap = move(newHeap);
    }

public:
    TaskManager() : nextId(1) {}

    string addTask(string name, string description, int priority, time_t deadline,
                   int duration, bool isRecurring = false, int recurringDays = 0) {
        Task* newTask = new Task(nextId++, name, description, priority, deadline,
                                 duration, isRecurring, recurringDays);
        allocatedTasks.push_back(newTask);
        taskHeap.push(newTask);
        taskMap[newTask->id] = newTask;
        allTasks.push_back(newTask);
        if (isRecurring) recurringQueue.push(newTask);
        undoStack.push({"ADD", newTask});
        while (!redoStack.empty()) redoStack.pop();
        return "{\"success\":true,\"message\":\"Task added successfully\",\"task\":" + newTask->toJSON() + "}";
    }

    string removeTask(int id) {
        if (taskMap.find(id) == taskMap.end()) {
            return "{\"success\":false,\"message\":\"Task not found\"}";
        }
        Task* task = taskMap[id];
        undoStack.push({"DELETE", task});
        while (!redoStack.empty()) redoStack.pop();
        taskMap.erase(id);
        for (int i = 0; i < (int)allTasks.size(); i++) {
            if (allTasks[i]->id == id) {
                allTasks.erase(allTasks.begin() + i);
                break;
            }
        }
        rebuildHeap();
        return "{\"success\":true,\"message\":\"Task deleted: " + task->name + "\"}";
    }

    string markCompleted(int id) {
        if (taskMap.find(id) == taskMap.end()) {
            return "{\"success\":false,\"message\":\"Task not found\"}";
        }
        Task* task = taskMap[id];
        task->completed = true;
        rebuildHeap();
        return "{\"success\":true,\"message\":\"Task marked as completed\"}";
    }

    string getAllTasksJSON() {
        stringstream ss;
        ss << "{\"success\":true,\"tasks\":[";
        for (size_t i = 0; i < allTasks.size(); i++) {
            ss << allTasks[i]->toJSON();
            if (i < allTasks.size() - 1) ss << ",";
        }
        ss << "]}";
        return ss.str();
    }

    string getPendingTasksJSON() {
        vector<Task*> pending;
        for (Task* t : allTasks) {
            if (!t->completed) pending.push_back(t);
        }
        stringstream ss;
        ss << "{\"success\":true,\"tasks\":[";
        for (size_t i = 0; i < pending.size(); i++) {
            ss << pending[i]->toJSON();
            if (i < pending.size() - 1) ss << ",";
        }
        ss << "]}";
        return ss.str();
    }

    string getTopTaskJSON() {
        if (taskHeap.empty()) {
            return "{\"success\":false,\"message\":\"No pending tasks\"}";
        }
        Task* top = taskHeap.top();
        return "{\"success\":true,\"task\":" + top->toJSON() + "}";
    }

    string getStatisticsJSON() {
        int total = allTasks.size();
        int completed = 0, pending = 0, overdue = 0;
        time_t now = time(0);
        for (Task* t : allTasks) {
            if (t->completed) {
                completed++;
            } else {
                pending++;
                if (t->deadline < now) overdue++;
            }
        }
        double completionRate = total > 0 ? (completed * 100.0) / total : 0;
        stringstream ss;
        ss << "{\"success\":true,";
        ss << "\"total\":" << total << ",";
        ss << "\"completed\":" << completed << ",";
        ss << "\"pending\":" << pending << ",";
        ss << "\"overdue\":" << overdue << ",";
        ss << "\"completionRate\":" << fixed << setprecision(1) << completionRate;
        ss << "}";
        return ss.str();
    }

    string undoOperation() {
        if (undoStack.empty()) {
            return "{\"success\":false,\"message\":\"Nothing to undo\"}";
        }
        auto operation = undoStack.top();
        undoStack.pop();
        if (operation.first == "ADD") {
            Task* t = operation.second;
            // remove from active lists but keep memory (so redo can bring it back)
            taskMap.erase(t->id);
            for (int i = 0; i < (int)allTasks.size(); i++) {
                if (allTasks[i]->id == t->id) {
                    allTasks.erase(allTasks.begin() + i);
                    break;
                }
            }
            rebuildHeap();
            redoStack.push(operation);
            return "{\"success\":true,\"message\":\"Undone: Task addition reverted\"}";
        } else if (operation.first == "DELETE") {
            Task* t = operation.second;
            taskMap[t->id] = t;
            allTasks.push_back(t);
            rebuildHeap();
            redoStack.push(operation);
            return "{\"success\":true,\"message\":\"Undone: Task restored\"}";
        }
        return "{\"success\":false,\"message\":\"Unknown operation\"}";
    }

    string redoOperation() {
        if (redoStack.empty()) {
            return "{\"success\":false,\"message\":\"Nothing to redo\"}";
        }
        auto operation = redoStack.top();
        redoStack.pop();
        if (operation.first == "ADD") {
            Task* t = operation.second;
            taskMap[t->id] = t;
            allTasks.push_back(t);
            rebuildHeap();
            undoStack.push(operation);
            return "{\"success\":true,\"message\":\"Redone: Task added back\"}";
        } else if (operation.first == "DELETE") {
            Task* t = operation.second;
            taskMap.erase(t->id);
            for (int i = 0; i < (int)allTasks.size(); i++) {
                if (allTasks[i]->id == t->id) {
                    allTasks.erase(allTasks.begin() + i);
                    break;
                }
            }
            rebuildHeap();
            undoStack.push(operation);
            return "{\"success\":true,\"message\":\"Redone: Task deleted again\"}";
        }
        return "{\"success\":false,\"message\":\"Unknown operation\"}";
    }

    string sortByPriority() {
        vector<Task*> temp = allTasks;
        sort(temp.begin(), temp.end(), [](Task* a, Task* b) {
            return a->priority < b->priority;
        });
        stringstream ss;
        ss << "{\"success\":true,\"tasks\":[";
        for (size_t i = 0; i < temp.size(); i++) {
            ss << temp[i]->toJSON();
            if (i < temp.size() - 1) ss << ",";
        }
        ss << "]}";
        return ss.str();
    }

    string sortByDeadline() {
        vector<Task*> temp = allTasks;
        sort(temp.begin(), temp.end(), [](Task* a, Task* b) {
            return a->deadline < b->deadline;
        });
        stringstream ss;
        ss << "{\"success\":true,\"tasks\":[";
        for (size_t i = 0; i < temp.size(); i++) {
            ss << temp[i]->toJSON();
            if (i < temp.size() - 1) ss << ",";
        }
        ss << "]}";
        return ss.str();
    }

    string sortByDuration() {
        vector<Task*> temp = allTasks;
        sort(temp.begin(), temp.end(), [](Task* a, Task* b) {
            return a->duration < b->duration;
        });
        stringstream ss;
        ss << "{\"success\":true,\"tasks\":[";
        for (size_t i = 0; i < temp.size(); i++) {
            ss << temp[i]->toJSON();
            if (i < temp.size() - 1) ss << ",";
        }
        ss << "]}";
        return ss.str();
    }

    string processRecurringTasks() {
        int size = (int)recurringQueue.size();
        time_t currentTime = time(0);
        int generated = 0;
        for (int i = 0; i < size; i++) {
            Task* t = recurringQueue.front();
            recurringQueue.pop();

            // Only consider if the task is a recurring task.
            if (t->isRecurring) {
                // generate a new task if original was completed OR original deadline passed
                if (t->completed || t->deadline < currentTime) {
                    time_t newDeadline = t->deadline;
                    if (t->recurringDays <= 0) {
                        // invalid recurringDays -> skip generation
                    } else {
                        while (newDeadline <= currentTime) {
                            newDeadline += (time_t)t->recurringDays * 86400;
                        }
                        addTask(t->name, t->description, t->priority, newDeadline,
                                t->duration, true, t->recurringDays);
                        generated++;
                    }
                }
            }
            recurringQueue.push(t);
        }
        if (generated > 0) {
            return "{\"success\":true,\"message\":\"Generated " + to_string(generated) + " recurring task(s)\"}";
        }
        return "{\"success\":true,\"message\":\"No recurring tasks to generate\"}";
    }

    ~TaskManager() {
        // delete allocated tasks (safe even if some are in allTasks)
        for (Task* t : allocatedTasks) {
            delete t;
        }
        allocatedTasks.clear();
    }
};

// -------------------- Helpers & HTTP handling --------------------
TaskManager taskManager;

string urlDecode(const string& str) {
    string result;
    for (size_t i = 0; i < str.length(); i++) {
        if (str[i] == '%' && i + 2 < str.length()) {
            string hex = str.substr(i + 1, 2);
            char ch = (char)strtol(hex.c_str(), nullptr, 16);
            result += ch;
            i += 2;
        } else if (str[i] == '+') {
            result += ' ';
        } else {
            result += str[i];
        }
    }
    return result;
}

unordered_map<string, string> parseQuery(const string& query) {
    unordered_map<string, string> params;
    stringstream ss(query);
    string pair;
    while (getline(ss, pair, '&')) {
        size_t pos = pair.find('=');
        if (pos != string::npos) {
            string key = urlDecode(pair.substr(0, pos));
            string value = urlDecode(pair.substr(pos + 1));
            params[key] = value;
        }
    }
    return params;
}

string handleRequest(const string& request) {
    size_t pos = request.find("GET /api/");
    if (pos == string::npos) return "";

    size_t endPos = request.find(" HTTP", pos);
    if (endPos == string::npos) return "";

    string path = request.substr(pos + 9, endPos - pos - 9);

    size_t queryPos = path.find('?');
    string endpoint = (queryPos != string::npos) ? path.substr(0, queryPos) : path;
    string query = (queryPos != string::npos) ? path.substr(queryPos + 1) : "";

    auto params = parseQuery(query);

    // safe conversion helpers
    auto getInt = [&](const string& key, int defaultVal)->int {
        if (params.find(key) == params.end()) return defaultVal;
        try { return stoi(params[key]); } catch(...) { return defaultVal; }
    };
    auto getLong = [&](const string& key, long defaultVal)->long {
        if (params.find(key) == params.end()) return defaultVal;
        try { return stol(params[key]); } catch(...) { return defaultVal; }
    };
    auto getBoolStr = [&](const string& key)->bool {
        if (params.find(key) == params.end()) return false;
        string v = params[key];
        return (v == "1" || v == "true" || v == "True");
    };
    auto getString = [&](const string& key, const string& defaultVal="")->string {
        if (params.find(key) == params.end()) return defaultVal;
        return params[key];
    };

    if (endpoint == "addTask") {
        string name = getString("name", "Untitled");
        string desc = getString("description", "");
        int priority = getInt("priority", 3);
        long deadline = getLong("deadline", time(0) + 3600);
        int duration = getInt("duration", 30);
        bool isRecurring = getBoolStr("isRecurring");
        int recurringDays = getInt("recurringDays", 0);
        return taskManager.addTask(name, desc, priority, (time_t)deadline, duration, isRecurring, recurringDays);
    } else if (endpoint == "removeTask") {
        int id = getInt("id", -1);
        if (id < 0) return "{\"success\":false,\"message\":\"Invalid id\"}";
        return taskManager.removeTask(id);
    } else if (endpoint == "markCompleted") {
        int id = getInt("id", -1);
        if (id < 0) return "{\"success\":false,\"message\":\"Invalid id\"}";
        return taskManager.markCompleted(id);
    } else if (endpoint == "getAllTasks") {
        return taskManager.getAllTasksJSON();
    } else if (endpoint == "getPendingTasks") {
        return taskManager.getPendingTasksJSON();
    } else if (endpoint == "getTopTask") {
        return taskManager.getTopTaskJSON();
    } else if (endpoint == "getStats") {
        return taskManager.getStatisticsJSON();
    } else if (endpoint == "undo") {
        return taskManager.undoOperation();
    } else if (endpoint == "redo") {
        return taskManager.redoOperation();
    } else if (endpoint == "sortByPriority") {
        return taskManager.sortByPriority();
    } else if (endpoint == "sortByDeadline") {
        return taskManager.sortByDeadline();
    } else if (endpoint == "sortByDuration") {
        return taskManager.sortByDuration();
    } else if (endpoint == "processRecurring") {
        return taskManager.processRecurringTasks();
    }

    return "{\"success\":false,\"message\":\"Unknown endpoint\"}";
}

// -------------------- Main (Winsock) --------------------
int main() {
    WSADATA wsaData;
    int wsaErr = WSAStartup(MAKEWORD(2,2), &wsaData);
    if (wsaErr != 0) {
        cerr << "WSAStartup failed: " << wsaErr << endl;
        return 1;
    }

    SOCKET server_fd = INVALID_SOCKET;
    SOCKET new_socket = INVALID_SOCKET;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);

    server_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_fd == INVALID_SOCKET) {
        cerr << "socket failed: " << WSAGetLastError() << endl;
        WSACleanup();
        return 1;
    }

    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt)) == SOCKET_ERROR) {
        cerr << "setsockopt failed: " << WSAGetLastError() << endl;
        closesocket(server_fd);
        WSACleanup();
        return 1;
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(8080);
    memset(address.sin_zero, 0, sizeof(address.sin_zero));

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) == SOCKET_ERROR) {
        cerr << "bind failed: " << WSAGetLastError() << endl;
        closesocket(server_fd);
        WSACleanup();
        return 1;
    }

    if (listen(server_fd, 10) == SOCKET_ERROR) {
        cerr << "listen failed: " << WSAGetLastError() << endl;
        closesocket(server_fd);
        WSACleanup();
        return 1;
    }

    cout << "Task Manager Server running on http://localhost:8080" << endl;
    cout << "Open task_manager.html in your browser to use the interface" << endl;

    while (true) {
        addrlen = sizeof(address);
        new_socket = accept(server_fd, (struct sockaddr *)&address, &addrlen);
        if (new_socket == INVALID_SOCKET) {
            cerr << "accept failed: " << WSAGetLastError() << endl;
            break;
        }

        // receive request
        char buffer[30000];
        int valread = recv(new_socket, buffer, (int)sizeof(buffer) - 1, 0);
        if (valread == SOCKET_ERROR) {
            cerr << "recv failed: " << WSAGetLastError() << endl;
            closesocket(new_socket);
            continue;
        }
        buffer[valread] = '\0';

        string request(buffer);
        string response;

        if (request.find("GET /api/") != string::npos) {
            string jsonResponse = handleRequest(request);
            response = "HTTP/1.1 200 OK\r\n";
            response += "Content-Type: application/json\r\n";
            response += "Access-Control-Allow-Origin: *\r\n";
            response += "Content-Length: " + to_string(jsonResponse.length()) + "\r\n";
            response += "\r\n";
            response += jsonResponse;
        } else {
            response = "HTTP/1.1 404 Not Found\r\n\r\n";
        }

        int sendRes = send(new_socket, response.c_str(), (int)response.length(), 0);
        if (sendRes == SOCKET_ERROR) {
            cerr << "send failed: " << WSAGetLastError() << endl;
        }
        closesocket(new_socket);
    }

    closesocket(server_fd);
    WSACleanup();
    return 0;
}
