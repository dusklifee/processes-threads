#include <windows.h>
#include <iostream>
#include <string>

using namespace std;

// Структура, которая будет лежать в общей памяти
struct SharedBuffer {
    bool dataReady;      // Флаг готовности (Семафорная логика)
    char command[256];   // Полезная нагрузка
    bool shouldExit;     // Сигнал к выходу
};

void runChild(const char* mName, const char* sName) {
    // Открываем существующие объекты по имени
    HANDLE hMutex = OpenMutexA(SYNCHRONIZE, FALSE, mName);
    HANDLE hMap = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, sName);
    
    if (!hMutex || !hMap) return; // Ошибка доступа

    SharedBuffer* data = (SharedBuffer*)MapViewOfFile(hMap, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedBuffer));

    cout << "[Child] Connected to Shared Memory. Waiting for commands..." << endl;

    while (true) {
        // ТОЧКА СИНХРОНИЗАЦИИ: Ждем права доступа к памяти
        WaitForSingleObject(hMutex, INFINITE);

        if (data->dataReady) {
            if (data->shouldExit) {
                ReleaseMutex(hMutex);
                break;
            }
            cout << "[Child] EXECUTING: " << data->command << endl;
            
            // Сбрасываем флаг - "ящик пуст"
            data->dataReady = false;
        }

        // Освобождаем мьютекс, чтобы родитель мог написать следующее
        ReleaseMutex(hMutex);
        Sleep(50); // Небольшая пауза для снижения нагрузки на CPU
    }

    UnmapViewOfFile(data);
    CloseHandle(hMap);
    CloseHandle(hMutex);
}

int main(int argc, char* argv[]) {
    // Используем Local\ чтобы работать без прав администратора
    const char* MUTEX_NAME = "Local\\LabMutexIPC";
    const char* MEM_NAME = "Local\\LabMemIPC";

    if (argc > 1 && string(argv[1]) == "child") {
        runChild(MUTEX_NAME, MEM_NAME);
        return 0;
    }

    // --- РОДИТЕЛЬ ---
    HANDLE hMutex = CreateMutexA(NULL, FALSE, MUTEX_NAME);
    HANDLE hMap = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, sizeof(SharedBuffer), MEM_NAME);
    
    // Отображаем память в адресное пространство
    SharedBuffer* data = (SharedBuffer*)MapViewOfFile(hMap, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedBuffer));
    
    // Инициализация
    data->dataReady = false;
    data->shouldExit = false;

    // Запуск ребенка
    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    string cmd = string(argv[0]) + " child";
    CreateProcessA(NULL, (LPSTR)cmd.c_str(), NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);

    string cmds[] = { "INIT_DB_CONNECTION", "QUERY_USER_DATA", "RENDER_UI" };

    for (const string& c : cmds) {
        Sleep(1500); // Имитация подготовки
        
        // Ждем мьютекс перед записью
        WaitForSingleObject(hMutex, INFINITE);
        
        // Пишем только если ребенок прочитал предыдущее (простейший спин-лок)
        if (!data->dataReady) {
            cout << "[Parent] Writing to Shared Memory: " << c << endl;
            strncpy_s(data->command, c.c_str(), _TRUNCATE);
            data->dataReady = true; // Поднимаем флаг
        }
        
        ReleaseMutex(hMutex);
    }

    // Отправка сигнала выхода
    Sleep(1000);
    WaitForSingleObject(hMutex, INFINITE);
    data->dataReady = true;
    data->shouldExit = true;
    ReleaseMutex(hMutex);

    WaitForSingleObject(pi.hProcess, INFINITE);
    cout << "[Parent] Child finished. Memory cleaned." << endl;

    UnmapViewOfFile(data);
    CloseHandle(hMap);
    CloseHandle(hMutex);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return 0;
}