// task_master.cpp
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>
#include <cctype>
#include <ctime>
#include <iomanip>
#include <chrono>
#include <regex>
#include <limits>

#ifdef _WIN32
    #include <windows.h>
#else
    #include <unistd.h>
#endif

using namespace std;

// ==================== Структуры данных ====================
struct SubTask {
    int taskNum;
    int subNum;
    string name;
    string status; // "todo" or "done"
};

struct Task {
    int id;
    string name;
    string deadline; // format: YYYY-MM-DD
    string status; // "todo" or "done"
    int priority; // 1-10, 1 самый высокий
    string comment;
    vector<SubTask> subtasks;
};

// ==================== Глобальные переменные ====================
const string TASKS_FILE = "tasks.txt";
const string ARCHIVE_FILE = "archive.txt";
bool fileOpened = false;

// ==================== Вспомогательные функции ====================
void clearScreen() {
#ifdef _WIN32
    system("cls");
#else
    cout << "\033[2J\033[1;1H";
    cout.flush();
#endif
}

void waitForEnter() {
    cout << "\n\033[33mНажмите Enter для продолжения...\033[0m";
    cin.ignore(numeric_limits<streamsize>::max(), '\n');
    cin.get();
}

string trim(const string& str) {
    size_t first = str.find_first_not_of(" \t\n\r");
    if (first == string::npos) return "";
    size_t last = str.find_last_not_of(" \t\n\r");
    return str.substr(first, last - first + 1);
}

string toLower(string str) {
    transform(str.begin(), str.end(), str.begin(), ::tolower);
    return str;
}

bool fileExists(const string& filename) {
    ifstream f(filename.c_str());
    return f.good();
}

string getCurrentDate() {
    auto now = chrono::system_clock::now();
    time_t now_time = chrono::system_clock::to_time_t(now);
    struct tm* parts = localtime(&now_time);
    char buffer[11];
    strftime(buffer, 11, "%Y-%m-%d", parts);
    return string(buffer);
}

int daysUntilDeadline(const string& deadline) {
    if (deadline.empty()) return -1;
    
    string current = getCurrentDate();
    
    int y1, m1, d1, y2, m2, d2;
    sscanf(current.c_str(), "%d-%d-%d", &y1, &m1, &d1);
    sscanf(deadline.c_str(), "%d-%d-%d", &y2, &m2, &d2);
    
    tm tm1 = {0}, tm2 = {0};
    tm1.tm_year = y1 - 1900;
    tm1.tm_mon = m1 - 1;
    tm1.tm_mday = d1;
    tm2.tm_year = y2 - 1900;
    tm2.tm_mon = m2 - 1;
    tm2.tm_mday = d2;
    
    time_t time1 = mktime(&tm1);
    time_t time2 = mktime(&tm2);
    
    double diff = difftime(time2, time1) / (60 * 60 * 24);
    return (int)diff;
}

string normalizeDate(const string& date) {
    if (date.length() != 10) return "";
    if (date[4] != '-' || date[7] != '-') return "";
    
    int year, month, day;
    if (sscanf(date.c_str(), "%d-%d-%d", &year, &month, &day) != 3) return "";
    
    // Корректировка месяца
    if (month < 1) month = 1;
    if (month > 12) month = 12;
    
    // Корректировка дня 
    int maxDay = 31;
    if (month == 4 || month == 6 || month == 9 || month == 11) maxDay = 30;
    else if (month == 2) {
        // Проверка на високосный год
        bool isLeap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
        maxDay = isLeap ? 29 : 28;
    }
    
    if (day < 1) day = 1;
    if (day > maxDay) day = maxDay;
    
    char buffer[11];
    snprintf(buffer, 11, "%04d-%02d-%02d", year, month, day);
    return string(buffer);
}

bool isValidDate(const string& date) {
    string normalized = normalizeDate(date);
    return !normalized.empty();
}

// ==================== Перенумерация задач ====================
void renumberTasks(vector<Task>& tasks) {
    sort(tasks.begin(), tasks.end(), [](const Task& a, const Task& b) {
        return a.id < b.id;
    });
    
    for (size_t i = 0; i < tasks.size(); i++) {
        int newId = i + 1;
        if (tasks[i].id != newId) {
            tasks[i].id = newId;
            for (auto& sub : tasks[i].subtasks) {
                sub.taskNum = newId;
            }
        }
    }
    
    for (auto& task : tasks) {
        sort(task.subtasks.begin(), task.subtasks.end(), [](const SubTask& a, const SubTask& b) {
            return a.subNum < b.subNum;
        });
        
        for (size_t i = 0; i < task.subtasks.size(); i++) {
            int newSubNum = i + 1;
            if (task.subtasks[i].subNum != newSubNum) {
                task.subtasks[i].subNum = newSubNum;
            }
        }
    }
}

// ==================== Работа с файлами ====================
vector<Task> loadTasks() {
    vector<Task> tasks;
    if (!fileExists(TASKS_FILE)) return tasks;
    
    ifstream file(TASKS_FILE);
    string line;
    
    while (getline(file, line)) {
        if (line.empty()) continue;
        
        stringstream ss(line);
        string type;
        getline(ss, type, '|');
        
        if (type == "TASK") {
            Task t;
            string idStr;
            getline(ss, idStr, '|');
            t.id = stoi(idStr);
            getline(ss, t.name, '|');
            getline(ss, t.deadline, '|');
            getline(ss, t.status, '|');
            string priorityStr;
            getline(ss, priorityStr, '|');
            t.priority = stoi(priorityStr);
            getline(ss, t.comment, '|');
            tasks.push_back(t);
        } else if (type == "SUBTASK") {
            string taskIdStr;
            getline(ss, taskIdStr, '|');
            int dotPos = taskIdStr.find('.');
            int taskId = stoi(taskIdStr.substr(0, dotPos));
            int subId = stoi(taskIdStr.substr(dotPos + 1));
            string name, status;
            getline(ss, name, '|');
            getline(ss, status, '|');
            
            for (auto& t : tasks) {
                if (t.id == taskId) {
                    SubTask st;
                    st.taskNum = taskId;
                    st.subNum = subId;
                    st.name = name;
                    st.status = status;
                    t.subtasks.push_back(st);
                    break;
                }
            }
        }
    }
    
    renumberTasks(tasks);
    return tasks;
}

void saveTasks(const vector<Task>& tasks) {
    ofstream file(TASKS_FILE);
    
    for (const auto& task : tasks) {
        file << "TASK|" << task.id << "|" << task.name << "|"
             << task.deadline << "|" << task.status << "|"
             << task.priority << "|" << task.comment << "\n";
        
        for (const auto& sub : task.subtasks) {
            file << "SUBTASK|" << sub.taskNum << "." << sub.subNum << "|"
                 << sub.name << "|" << sub.status << "\n";
        }
    }
}

vector<Task> loadArchive() {
    vector<Task> archive;
    if (!fileExists(ARCHIVE_FILE)) return archive;
    
    ifstream file(ARCHIVE_FILE);
    string line;
    
    while (getline(file, line)) {
        if (line.empty()) continue;
        
        stringstream ss(line);
        string type;
        getline(ss, type, '|');
        
        if (type == "TASK") {
            Task t;
            string idStr;
            getline(ss, idStr, '|');
            t.id = stoi(idStr);
            getline(ss, t.name, '|');
            getline(ss, t.deadline, '|');
            getline(ss, t.status, '|');
            string priorityStr;
            getline(ss, priorityStr, '|');
            t.priority = stoi(priorityStr);
            getline(ss, t.comment, '|');
            archive.push_back(t);
        } else if (type == "SUBTASK") {
            string taskIdStr;
            getline(ss, taskIdStr, '|');
            int dotPos = taskIdStr.find('.');
            int taskId = stoi(taskIdStr.substr(0, dotPos));
            int subId = stoi(taskIdStr.substr(dotPos + 1));
            string name, status;
            getline(ss, name, '|');
            getline(ss, status, '|');
            
            for (auto& t : archive) {
                if (t.id == taskId) {
                    SubTask st;
                    st.taskNum = taskId;
                    st.subNum = subId;
                    st.name = name;
                    st.status = status;
                    t.subtasks.push_back(st);
                    break;
                }
            }
        }
    }
    
    return archive;
}

void saveArchive(const vector<Task>& archive) {
    ofstream file(ARCHIVE_FILE);
    
    for (const auto& task : archive) {
        file << "TASK|" << task.id << "|" << task.name << "|"
             << task.deadline << "|" << task.status << "|"
             << task.priority << "|" << task.comment << "\n";
        
        for (const auto& sub : task.subtasks) {
            file << "SUBTASK|" << sub.taskNum << "." << sub.subNum << "|"
                 << sub.name << "|" << sub.status << "\n";
        }
    }
}

// ==================== Отображение команд ====================
void showCommands() {
    cout << "\033[1;36m=== Помощник задач ===\033[0m\n";
    cout << "\033[33mopen <filename>              \033[0m- открыть файл задач\n";
    cout << "\033[33madd task <название> [дедлайн] [приоритет] \033[0m- добавить задачу\n";
    cout << "\033[33madd subtask <номер_задачи> <название> \033[0m- добавить подзадачу\n";
    cout << "\033[33mdelete task <номер>          \033[0m- удалить задачу\n";
    cout << "\033[33mdelete subtask <задача> <подзадача> \033[0m- удалить подзадачу\n";
    cout << "\033[33mdone task <номер>            \033[0m- отметить задачу выполненной\n";
    cout << "\033[33mdone subtask <задача> <подзадача> \033[0m- отметить подзадачу выполненной\n";
    cout << "\033[33mall done                     \033[0m- отметить все задачи выполненными\n";
    cout << "\033[33mundone task <номер>          \033[0m- отметить задачу невыполненной\n";
    cout << "\033[33mundone subtask <задача> <подзадача> \033[0m- отметить подзадачу невыполненной\n";
    cout << "\033[33mcomment task <номер> <текст> \033[0m- добавить комментарий к задаче\n";
    cout << "\033[33msearch task <название>       \033[0m- поиск задачи\n";
    cout << "\033[33mlist num                     \033[0m- вывести задачи по номерам\n";
    cout << "\033[33mlist priority                \033[0m- вывести задачи по приоритету\n";
    cout << "\033[33mlist deadline                \033[0m- вывести задачи по дедлайну\n";
    cout << "\033[33mlist status                  \033[0m- вывести задачи по статусу (сначала выполненные)\n";
    cout << "\033[33mlist statusN                 \033[0m- вывести задачи по статусу (сначала невыполненные)\n";
    cout << "\033[33mclear done                   \033[0m- очистить выполненные задачи (в архив)\n";
    cout << "\033[33mclear all                    \033[0m- очистить все задачи\n";
    cout << "\033[33marchive                      \033[0m- показать архив\n";
    cout << "\033[33msearch arch <название>       \033[0m- поиск в архиве\n";
    cout << "\033[33mexit                         \033[0m- выход\n";
    cout << "═══════════════════════════════════════════════════════════════════\n";
}

void printStatusIcon(const string& status) {
    if (status == "done") {
        cout << "\033[32m●\033[0m";
    } else {
        cout << "\033[31m●\033[0m";
    }
}

void printTask(const Task& task, bool showSubtasks = true) {
    string deadlineDisplay = task.deadline.empty() ? "Нет дедлайна" : task.deadline;
    string daysDisplay = "";
    
    if (!task.deadline.empty()) {
        int daysLeft = daysUntilDeadline(task.deadline);
        daysDisplay = " {" + to_string(daysLeft) + " дн.}";
    }
    
    cout << "\n\033[1mЗадача №" << task.id << ":\033[0m " << task.name << daysDisplay << " ";
    printStatusIcon(task.status);
    cout << " \033[36m<" << task.priority << ">\033[0m\n";
    cout << "    Дедлайн: " << deadlineDisplay << "\n";
    
    if (!task.comment.empty()) {
        cout << "    Комментарий: " << task.comment << "\n";
    }
    
    if (showSubtasks && !task.subtasks.empty()) {
        cout << "    Подзадачи:\n";
        for (const auto& sub : task.subtasks) {
            cout << "        " << sub.taskNum << "." << sub.subNum 
                 << " [" << sub.name << "] ";
            printStatusIcon(sub.status);
            cout << "\n";
        }
    }
}

void listTasks(vector<Task>& tasks, const string& order) {
    if (!fileOpened) { 
        cout << "\033[31mОшибка: сначала откройте файл командой open\033[0m\n";
        waitForEnter();
        return;
    }
    
    if (tasks.empty()) {
        cout << "\n\033[33mСписок задач пуст.\033[0m\n";
        waitForEnter();
        return;
    }
    
    vector<Task> sorted = tasks;
    
    if (order == "num") {
        sort(sorted.begin(), sorted.end(), [](const Task& a, const Task& b) {
            return a.id < b.id;
        });
    } else if (order == "priority") {
        sort(sorted.begin(), sorted.end(), [](const Task& a, const Task& b) {
            return a.priority < b.priority;
        });
    } else if (order == "deadline") {
        sort(sorted.begin(), sorted.end(), [](const Task& a, const Task& b) {
            int daysA = a.deadline.empty() ? 999999 : daysUntilDeadline(a.deadline);
            int daysB = b.deadline.empty() ? 999999 : daysUntilDeadline(b.deadline);
            return daysA < daysB;
        });
    } else if (order == "status") {
        sort(sorted.begin(), sorted.end(), [](const Task& a, const Task& b) {
            if (a.status == b.status) return a.id < b.id;
            return a.status > b.status;
        });
    } else if (order == "statusN") {
        sort(sorted.begin(), sorted.end(), [](const Task& a, const Task& b) {
            if (a.status == b.status) return a.id < b.id;
            return a.status < b.status;
        });
    }
    
    cout << "\n\033[1;36m=== Список задач ===\033[0m\n";
    for (const auto& task : sorted) {
        printTask(task, true);
    }
    cout << "\n═══════════════════════════════════════════════════════════════════\n";
    waitForEnter();
}

// ==================== Команды ====================
void addTask(vector<Task>& tasks, const string& name, const string& deadline, int priority) {
    if (!fileOpened) { 
        cout << "\033[31mОшибка: сначала откройте файл командой open\033[0m\n";
        return;
    }
    
    int newId = tasks.empty() ? 1 : tasks.back().id + 1;
    
    Task newTask;
    newTask.id = newId;
    newTask.name = name;
    newTask.deadline = deadline;
    newTask.status = "todo";
    newTask.priority = priority;
    newTask.comment = "";
    
    tasks.push_back(newTask);
    saveTasks(tasks);
    cout << "\n\033[32m✓ Задача #" << newId << " добавлена: \"" << name << "\"\033[0m\n";
    if (!deadline.empty()) {
        cout << "   Дедлайн: " << deadline << "\n";
    }
    cout << "   Приоритет: " << priority << "\n";
}

void addSubtask(vector<Task>& tasks, int taskNum, const string& name) {
    if (!fileOpened) { 
        cout << "\033[31mОшибка: сначала откройте файл командой open\033[0m\n";
        return;
    }
    
    for (auto& task : tasks) {
        if (task.id == taskNum) {
            int newSubNum = task.subtasks.size() + 1;
            
            SubTask newSub;
            newSub.taskNum = taskNum;
            newSub.subNum = newSubNum;
            newSub.name = name;
            newSub.status = (task.status == "done") ? "done" : "todo";
            
            task.subtasks.push_back(newSub);
            saveTasks(tasks);
            cout << "\n\033[32m✓ Подзадача " << taskNum << "." << newSubNum 
                 << " добавлена: \"" << name << "\"\033[0m\n";
            if (task.status == "done") {
                cout << "   \033[33mПримечание: задача выполнена, поэтому подзадача создана выполненной\033[0m\n";
            }
            return;
        }
    }
    cout << "\n\033[31mОшибка: задача #" << taskNum << " не найдена\033[0m\n";
}

void allDone(vector<Task>& tasks) {
    if (!fileOpened) { 
        cout << "\033[31mОшибка: сначала откройте файл командой open\033[0m\n";
        return;
    }
    
    char confirm;
    cout << "\n\033[33mВы уверены, что хотите отметить ВСЕ задачи как выполненные? (y/n): \033[0m";
    cin >> confirm;
    cin.ignore();
    
    if (confirm == 'y' || confirm == 'Y') {
        for (auto& task : tasks) {
            task.status = "done";
            for (auto& sub : task.subtasks) {
                sub.status = "done";
            }
        }
        saveTasks(tasks);
        cout << "\n\033[32m✓ Все задачи отмечены как выполненные\033[0m\n";
    } else {
        cout << "\n\033[33mОперация отменена\033[0m\n";
    }
}

void doneTask(vector<Task>& tasks, int taskNum) {
    if (!fileOpened) { 
        cout << "\033[31mОшибка: сначала откройте файл командой open\033[0m\n";
        return;
    }
    
    for (auto& task : tasks) {
        if (task.id == taskNum) {
            if (task.status == "done") {
                cout << "\n\033[33mЗадача #" << taskNum << " уже выполнена\033[0m\n";
                return;
            }
            task.status = "done";
            for (auto& sub : task.subtasks) {
                sub.status = "done";
            }
            saveTasks(tasks);
            cout << "\n\033[32m✓ Задача #" << taskNum << " отмечена как выполненная\033[0m\n";
            cout << "   Все подзадачи также отмечены выполненными\n";
            return;
        }
    }
    cout << "\n\033[31mОшибка: задача #" << taskNum << " не найдена\033[0m\n";
}

void doneSubtask(vector<Task>& tasks, int taskNum, int subNum) {
    if (!fileOpened) { 
        cout << "\033[31mОшибка: сначала откройте файл командой open\033[0m\n";
        return;
    }
    
    for (auto& task : tasks) {
        if (task.id == taskNum) {
            if (subNum < 1 || subNum > (int)task.subtasks.size()) {
                cout << "\n\033[31mОшибка: подзадача #" << taskNum << "." << subNum 
                     << " не найдена\033[0m\n";
                return;
            }
            
            auto& sub = task.subtasks[subNum - 1];
            if (sub.status == "done") {
                cout << "\n\033[33mПодзадача " << taskNum << "." << subNum 
                     << " уже выполнена\033[0m\n";
                return;
            }
            sub.status = "done";
            saveTasks(tasks);
            cout << "\n\033[32m✓ Подзадача " << taskNum << "." << subNum 
                 << " отмечена как выполненная\033[0m\n";
            return;
        }
    }
    cout << "\n\033[31mОшибка: задача #" << taskNum << " не найдена\033[0m\n";
}

void undoneTask(vector<Task>& tasks, int taskNum) {
    if (!fileOpened) { 
        cout << "\033[31mОшибка: сначала откройте файл командой open\033[0m\n";
        return;
    }
    
    for (auto& task : tasks) {
        if (task.id == taskNum) {
            if (task.status == "todo") {
                cout << "\n\033[33mЗадача #" << taskNum << " уже не выполнена\033[0m\n";
                return;
            }
            task.status = "todo";
            saveTasks(tasks);
            cout << "\n\033[32m✓ Задача #" << taskNum << " отмечена как невыполненная\033[0m\n";
            cout << "   \033[33mПримечание: подзадачи остались выполненными\033[0m\n";
            return;
        }
    }
    cout << "\n\033[31mОшибка: задача #" << taskNum << " не найдена\033[0m\n";
}

void undoneSubtask(vector<Task>& tasks, int taskNum, int subNum) {
    if (!fileOpened) { 
        cout << "\033[31mОшибка: сначала откройте файл командой open\033[0m\n";
        return;
    }
    
    for (auto& task : tasks) {
        if (task.id == taskNum) {
            if (subNum < 1 || subNum > (int)task.subtasks.size()) {
                cout << "\n\033[31mОшибка: подзадача #" << taskNum << "." << subNum 
                     << " не найдена\033[0m\n";
                return;
            }
            
            auto& sub = task.subtasks[subNum - 1];
            if (sub.status == "todo") {
                cout << "\n\033[33mПодзадача " << taskNum << "." << subNum 
                     << " уже не выполнена\033[0m\n";
                return;
            }
            sub.status = "todo";
            saveTasks(tasks);
            cout << "\n\033[32m✓ Подзадача " << taskNum << "." << subNum 
                 << " отмечена как невыполненная\033[0m\n";
            return;
        }
    }
    cout << "\n\033[31mОшибка: задача #" << taskNum << " не найдена\033[0m\n";
}

void deleteTask(vector<Task>& tasks, int taskNum) {
    if (!fileOpened) { 
        cout << "\033[31mОшибка: сначала откройте файл командой open\033[0m\n";
        return;
    }
    
    for (size_t i = 0; i < tasks.size(); i++) {
        if (tasks[i].id == taskNum) {
            tasks.erase(tasks.begin() + i);
            renumberTasks(tasks);
            saveTasks(tasks);
            cout << "\n\033[32m✓ Задача #" << taskNum << " удалена. Задачи перенумерованы.\033[0m\n";
            return;
        }
    }
    cout << "\n\033[31mОшибка: задача #" << taskNum << " не найдена\033[0m\n";
}

void deleteSubtask(vector<Task>& tasks, int taskNum, int subNum) {
    if (!fileOpened) { 
        cout << "\033[31mОшибка: сначала откройте файл командой open\033[0m\n";
        return;
    }
    
    for (auto& task : tasks) {
        if (task.id == taskNum) {
            if (subNum < 1 || subNum > (int)task.subtasks.size()) {
                cout << "\n\033[31mОшибка: подзадача #" << taskNum << "." << subNum 
                     << " не найдена\033[0m\n";
                return;
            }
            
            task.subtasks.erase(task.subtasks.begin() + (subNum - 1));
            
            for (size_t i = 0; i < task.subtasks.size(); i++) {
                task.subtasks[i].subNum = i + 1;
            }
            
            saveTasks(tasks);
            cout << "\n\033[32m✓ Подзадача " << taskNum << "." << subNum 
                 << " удалена. Подзадачи перенумерованы.\033[0m\n";
            return;
        }
    }
    cout << "\n\033[31mОшибка: задача #" << taskNum << " не найдена\033[0m\n";
}

void commentTask(vector<Task>& tasks, int taskNum, const string& comment) {
    if (!fileOpened) { 
        cout << "\033[31mОшибка: сначала откройте файл командой open\033[0m\n";
        return;
    }
    
    if (comment.length() > 150) {
        cout << "\n\033[31mОшибка: комментарий не должен превышать 150 символов\033[0m\n";
        return;
    }
    
    for (auto& task : tasks) {
        if (task.id == taskNum) {
            task.comment = comment;
            saveTasks(tasks);
            cout << "\n\033[32m✓ Комментарий к задаче #" << taskNum << " добавлен\033[0m\n";
            return;
        }
    }
    cout << "\n\033[31mОшибка: задача #" << taskNum << " не найдена\033[0m\n";
}

void searchTask(const vector<Task>& tasks, const string& query) {
    if (!fileOpened) { 
        cout << "\033[31mОшибка: сначала откройте файл командой open\033[0m\n";
        waitForEnter();
        return;
    }
    
    string lowerQuery = toLower(query);
    bool found = false;
    
    cout << "\n\033[1;36m=== Результаты поиска: \"" << query << "\" ===\033[0m\n";
    for (const auto& task : tasks) {
        if (toLower(task.name).find(lowerQuery) != string::npos) {
            printTask(task, false);
            found = true;
        }
    }
    
    if (!found) {
        cout << "\n\033[33mЗадачи не найдены\033[0m\n";
    }
    cout << "\n═══════════════════════════════════════════════════════════════════\n";
    waitForEnter();
}

void clearDoneTasks(vector<Task>& tasks, vector<Task>& archive) {
    if (!fileOpened) { 
        cout << "\033[31mОшибка: сначала откройте файл командой open\033[0m\n";
        return;
    }
    
    char confirm;
    cout << "\n\033[33mВы уверены, что хотите переместить все выполненные задачи в архив? (y/n): \033[0m";
    cin >> confirm;
    cin.ignore();
    
    if (confirm == 'y' || confirm == 'Y') {
        vector<Task> remaining;
        for (auto& task : tasks) {
            if (task.status == "done") {
                archive.push_back(task);
            } else {
                remaining.push_back(task);
            }
        }
        
        tasks = remaining;
        renumberTasks(tasks);
        saveTasks(tasks);
        saveArchive(archive);
        cout << "\n\033[32m✓ Выполненные задачи перемещены в архив. Задачи перенумерованы.\033[0m\n";
    } else {
        cout << "\n\033[33mОперация отменена\033[0m\n";
    }
}

void clearAllTasks(vector<Task>& tasks) {
    if (!fileOpened) { 
        cout << "\033[31mОшибка: сначала откройте файл командой open\033[0m\n";
        return;
    }
    
    char confirm;
    cout << "\n\033[31mВы уверены, что хотите удалить ВСЕ задачи без возможности восстановления? (y/n): \033[0m";
    cin >> confirm;
    cin.ignore();
    
    if (confirm == 'y' || confirm == 'Y') {
        tasks.clear();
        saveTasks(tasks);
        cout << "\n\033[32m✓ Все задачи удалены\033[0m\n";
    } else {
        cout << "\n\033[33mОперация отменена\033[0m\n";
    }
}

void showArchive(const vector<Task>& archive) {
    if (!fileOpened) { 
        cout << "\033[31mОшибка: сначала откройте файл командой open\033[0m\n";
        waitForEnter();
        return;
    }
    
    if (archive.empty()) {
        cout << "\n\033[33mАрхив пуст\033[0m\n";
        waitForEnter();
        return;
    }
    
    cout << "\n\033[1;36m=== АРХИВ ===\033[0m\n";
    for (const auto& task : archive) {
        cout << "\n\033[1mЗадача #" << task.id << ":\033[0m " << task.name 
             << " \033[36m<" << task.priority << ">\033[0m\n";
        
        string deadlineDisplay = task.deadline.empty() ? "Нет дедлайна" : task.deadline;
        cout << "    Дедлайн: " << deadlineDisplay << "\n";
        
        if (!task.comment.empty()) {
            cout << "    Комментарий: " << task.comment << "\n";
        }
        
        if (!task.subtasks.empty()) {
            cout << "    Подзадачи:\n";
            for (const auto& sub : task.subtasks) {
                cout << "        " << sub.taskNum << "." << sub.subNum 
                     << " [" << sub.name << "]\n";
            }
        }
    }
    cout << "\n═══════════════════════════════════════════════════════════════════\n";
    waitForEnter();
}

void searchArchive(const vector<Task>& archive, const string& query) {
    if (!fileOpened) { 
        cout << "\033[31mОшибка: сначала откройте файл командой open\033[0m\n";
        waitForEnter();
        return;
    }
    
    string lowerQuery = toLower(query);
    bool found = false;
    
    cout << "\n\033[1;36m=== Результаты поиска в архиве: \"" << query << "\" ===\033[0m\n";
    for (const auto& task : archive) {
        if (toLower(task.name).find(lowerQuery) != string::npos) {
            cout << "\n\033[1mЗадача #" << task.id << ":\033[0m " << task.name 
                 << " \033[36m<" << task.priority << ">\033[0m\n";
            
            string deadlineDisplay = task.deadline.empty() ? "Нет дедлайна" : task.deadline;
            cout << "    Дедлайн: " << deadlineDisplay << "\n";
            
            if (!task.comment.empty()) {
                cout << "    Комментарий: " << task.comment << "\n";
            }
            found = true;
        }
    }
    
    if (!found) {
        cout << "\n\033[33mВ архиве ничего не найдено\033[0m\n";
    }
    cout << "\n═══════════════════════════════════════════════════════════════════\n";
    waitForEnter();
}

void openFile(vector<Task>& tasks, vector<Task>& archive, const string& filename) {
    if (filename == "tasks") {
        tasks = loadTasks();
        archive = loadArchive();
        fileOpened = true;
        cout << "\n\033[32m✓ Файл " << filename << ".txt открыт\033[0m\n";
        cout << "   Загружено задач: " << tasks.size() << "\n";
        cout << "   Загружено архивных задач: " << archive.size() << "\n";
    } else {
        cout << "\n\033[31mОшибка: можно открыть только файл 'tasks'\033[0m\n";
    }
}

// ==================== Парсинг команды add task ====================
void parseAddTask(vector<Task>& tasks, const string& args) {
    string name;
    string deadline = "";
    int priority = 5;
    
    // Разбиваем аргументы на слова
    vector<string> words;
    stringstream ss(args);
    string word;
    while (ss >> word) {
        words.push_back(word);
    }
    
    // Ищем дату и приоритет среди слов
    for (size_t i = 0; i < words.size(); i++) {
        // Проверка на дату (содержит два дефиса)
        if (words[i].find('-') != string::npos && 
            count(words[i].begin(), words[i].end(), '-') == 2) {
            string normalized = normalizeDate(words[i]);
            if (!normalized.empty()) {
                deadline = normalized;
                words.erase(words.begin() + i);
                i--;
            }
        }
    }
    
    for (size_t i = 0; i < words.size(); i++) {
        if (!words[i].empty() && all_of(words[i].begin(), words[i].end(), ::isdigit)) {
            int p = stoi(words[i]);
            if (p >= 1 && p <= 10) {
                priority = p;
                words.erase(words.begin() + i);
                i--;
            }
        }
    }
    
    // Оставшиеся слова - это название
    name = "";
    for (size_t i = 0; i < words.size(); i++) {
        if (i > 0) name += " ";
        name += words[i];
    }
    
    name = trim(name);
    
    if (name.empty()) {
        cout << "\033[31mОшибка: укажите название задачи\033[0m\n";
    } else {
        addTask(tasks, name, deadline, priority);
    }
}

// ==================== Главная функция ====================
int main() {
    vector<Task> tasks;
    vector<Task> archive;
    
    string inputLine;
    string lastOutput = "";
    bool waitingForClear = false;
    
    while (true) {
        if (!waitingForClear) {
            clearScreen();
            showCommands();
            
            if (!lastOutput.empty()) {
                cout << lastOutput << "\n";
                lastOutput = "";
            }
            
            cout << "\n\033[1;32m> \033[0m";
        } else {
            cout << "\n\033[1;32m> \033[0m";
            waitingForClear = false;
        }
        
        getline(cin, inputLine);
        
        if (inputLine.empty()) {
            continue;
        }
        
        stringstream ss(inputLine);
        string command;
        ss >> command;
        
        command = toLower(command);
        
        if (command == "exit") {
            clearScreen();
            cout << "=== Помощник задач ===\n";
            cout << "До свидания!\n";
            break;
        }
        else if (command == "cmd") {
            lastOutput = "";
            waitingForClear = true;
            continue;
        }
        else if (command == "open") {
            string filename;
            ss >> filename;
            
            openFile(tasks, archive, filename);
            lastOutput = "";
            waitingForClear = true;
            continue;
        }
        else if (command == "add") {
            string type;
            ss >> type;
            
            if (type == "task") {
                string args;
                getline(ss, args);
                args = trim(args);
                
                if (args.empty()) {
                    lastOutput = "\033[31mОшибка: укажите название задачи\033[0m";
                } else {
                    parseAddTask(tasks, args);
                    lastOutput = "";
                }
                waitingForClear = true;
                continue;
            }
            else if (type == "subtask") {
                int taskNum;
                ss >> taskNum;
                string name;
                getline(ss, name);
                name = trim(name);
                
                if (name.empty()) {
                    lastOutput = "\033[31mОшибка: укажите название подзадачи\033[0m";
                } else {
                    addSubtask(tasks, taskNum, name);
                    lastOutput = "";
                }
                waitingForClear = true;
                continue;
            }
            else {
                lastOutput = "\033[31mОшибка: используйте 'add task' или 'add subtask'\033[0m";
                waitingForClear = true;
                continue;
            }
        }
        else if (command == "all" && ss >> command && command == "done") {
            allDone(tasks);
            waitingForClear = true;
            continue;
        }
        else if (command == "list") {
            string order;
            ss >> order;
            
            if (order == "num" || order == "priority" || order == "deadline" || 
                order == "status" || order == "statusN") {
                listTasks(tasks, order);
                waitingForClear = false;
            } else {
                lastOutput = "\033[31mОшибка: используйте list num, list priority, list deadline, list status или list statusN\033[0m";
                waitingForClear = true;
                continue;
            }
        }
        else if (command == "done") {
            string type;
            ss >> type;
            
            if (type == "task") {
                int taskNum;
                ss >> taskNum;
                doneTask(tasks, taskNum);
                waitingForClear = true;
                continue;
            }
            else if (type == "subtask") {
                int taskNum, subNum;
                ss >> taskNum >> subNum;
                doneSubtask(tasks, taskNum, subNum);
                waitingForClear = true;
                continue;
            }
            else {
                lastOutput = "\033[31mОшибка: используйте 'done task' или 'done subtask'\033[0m";
                waitingForClear = true;
                continue;
            }
        }
        else if (command == "undone") {
            string type;
            ss >> type;
            
            if (type == "task") {
                int taskNum;
                ss >> taskNum;
                undoneTask(tasks, taskNum);
                waitingForClear = true;
                continue;
            }
            else if (type == "subtask") {
                int taskNum, subNum;
                ss >> taskNum >> subNum;
                undoneSubtask(tasks, taskNum, subNum);
                waitingForClear = true;
                continue;
            }
            else {
                lastOutput = "\033[31mОшибка: используйте 'undone task' или 'undone subtask'\033[0m";
                waitingForClear = true;
                continue;
            }
        }
        else if (command == "delete") {
            string type;
            ss >> type;
            
            if (type == "task") {
                int taskNum;
                ss >> taskNum;
                deleteTask(tasks, taskNum);
                waitingForClear = true;
                continue;
            }
            else if (type == "subtask") {
                int taskNum, subNum;
                ss >> taskNum >> subNum;
                deleteSubtask(tasks, taskNum, subNum);
                waitingForClear = true;
                continue;
            }
            else {
                lastOutput = "\033[31mОшибка: используйте 'delete task' или 'delete subtask'\033[0m";
                waitingForClear = true;
                continue;
            }
        }
        else if (command == "comment") {
            string type;
            ss >> type;
            
            if (type == "task") {
                int taskNum;
                ss >> taskNum;
                string comment;
                getline(ss, comment);
                comment = trim(comment);
                
                if (comment.empty()) {
                    lastOutput = "\033[31mОшибка: введите текст комментария\033[0m";
                } else {
                    commentTask(tasks, taskNum, comment);
                    lastOutput = "";
                }
                waitingForClear = true;
                continue;
            }
            else {
                lastOutput = "\033[31mОшибка: используйте 'comment task'\033[0m";
                waitingForClear = true;
                continue;
            }
        }
        else if (command == "search") {
            string type;
            ss >> type;
            
            if (type == "task") {
                string query;
                getline(ss, query);
                query = trim(query);
                
                if (query.empty()) {
                    lastOutput = "\033[31mОшибка: введите поисковый запрос\033[0m";
                    waitingForClear = true;
                    continue;
                } else {
                    searchTask(tasks, query);
                    waitingForClear = false;
                    continue;
                }
            }
            else if (type == "arch") {
                string query;
                getline(ss, query);
                query = trim(query);
                
                if (query.empty()) {
                    lastOutput = "\033[31mОшибка: введите поисковый запрос\033[0m";
                    waitingForClear = true;
                    continue;
                } else {
                    searchArchive(archive, query);
                    waitingForClear = false;
                    continue;
                }
            }
            else {
                lastOutput = "\033[31mОшибка: используйте 'search task' или 'search arch'\033[0m";
                waitingForClear = true;
                continue;
            }
        }
        else if (command == "clear") {
            string type;
            ss >> type;
            
            if (type == "done") {
                clearDoneTasks(tasks, archive);
                waitingForClear = true;
                continue;
            }
            else if (type == "all") {
                clearAllTasks(tasks);
                waitingForClear = true;
                continue;
            }
            else {
                lastOutput = "\033[31mОшибка: используйте 'clear done' или 'clear all'\033[0m";
                waitingForClear = true;
                continue;
            }
        }
        else if (command == "archive") {
            showArchive(archive);
            waitingForClear = false;
            continue;
        }
        else {
            lastOutput = "\033[31mНеизвестная команда. Введите 'cmd' для списка команд\033[0m";
            waitingForClear = true;
            continue;
        }
    }
    
    return 0;
}