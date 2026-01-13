#include <iostream>
#include <unistd.h>
#include <sys/wait.h>
#include <string.h>
#include <vector>

using namespace std;

int main() {
    int pipefd[2]; // Дескрипторы: [0]-чтение, [1]-запись
    if (pipe(pipefd) == -1) { perror("pipe"); return 1; }

    pid_t pid = fork();

    if (pid == 0) { 
        // --- ДОЧЕРНИЙ ПРОЦЕСС (ИСПОЛНИТЕЛЬ) ---
        close(pipefd[1]); // Нам не нужна запись
        char buffer[256];
        
        cout << "[Child] Worker ready. Waiting for stream..." << endl;
        
        while (true) {
            // ТОЧКА СИНХРОНИЗАЦИИ: read блокирует процесс
            ssize_t bytes = read(pipefd[0], buffer, sizeof(buffer)-1);
            
            if (bytes <= 0) break; // 0 означает EOF (родитель закрыл канал)
            
            buffer[bytes] = '\0';
            cout << "[Child] Processing task: " << buffer << endl;
        }
        
        cout << "[Child] Stream closed. Shutting down." << endl;
        close(pipefd[0]);
        return 0;
    } 
    else { 
        // --- РОДИТЕЛЬСКИЙ ПРОЦЕСС (ДИСПЕТЧЕР) ---
        close(pipefd[0]); // Нам не нужно чтение
        
        vector<string> tasks = {"IMAGE_PROCESS_01", "CALC_HASH_X86", "UPLOAD_LOGS"};

        for (const string& t : tasks) {
            sleep(1); // Имитация работы
            cout << "[Parent] Sending: " << t << endl;
            // Проталкиваем данные в буфер ядра
            write(pipefd[1], t.c_str(), t.length());
        }

        cout << "[Parent] All tasks sent. Closing pipe." << endl;
        close(pipefd[1]); // Посылаем сигнал EOF ребенку
        wait(NULL);       // Ждем полного завершения
    }
    return 0;
}