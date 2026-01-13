#include <windows.h>
#include <iostream>
#include <vector>
#include <string>
#include <cmath>
#include <algorithm>
#include <numeric>

using namespace std;

// Константы
const int THREAD_COUNT = 4;
const int DATA_SIZE = 100000;
const int BUFFER_SIZE = 10;

// Глобальные данные
vector<int> g_data(DATA_SIZE);
vector<double> g_results(DATA_SIZE);
vector<int> g_buffer(BUFFER_SIZE);
int g_buffer_index = 0;

// Класс кастомного семафора на базе критической секции
class CustomSemaphore {
private:
    CRITICAL_SECTION m_cs;          // Защита внутреннего счетчика
    int m_count;                    // Текущее значение семафора
    int m_maxCount;                 // Максимальное значение
    int m_waitingThreads;           // Количество ожидающих потоков

public:
    CustomSemaphore(int initialCount, int maxCount) {
        InitializeCriticalSection(&m_cs);
        m_count = initialCount;
        m_maxCount = maxCount;
        m_waitingThreads = 0;
    }

    ~CustomSemaphore() {
        DeleteCriticalSection(&m_cs);
    }

    // Аналог WaitForSingleObject(semaphore, INFINITE)
    void Wait() {
        EnterCriticalSection(&m_cs);

        m_waitingThreads++;

        // Ждем в цикле, пока не появится свободное "разрешение"
        while (m_count <= 0) {
            LeaveCriticalSection(&m_cs);
            Sleep(1);  // Отпускаем мьютекс и даем другим потокам поработать
            EnterCriticalSection(&m_cs);
        }

        m_waitingThreads--;
        m_count--;  // Забираем одно "разрешение"

        LeaveCriticalSection(&m_cs);
    }

    // Аналог ReleaseSemaphore(semaphore, 1, NULL)
    void Release() {
        EnterCriticalSection(&m_cs);

        if (m_count < m_maxCount) {
            m_count++;
        }

        LeaveCriticalSection(&m_cs);
    }

    int GetCount() {
        EnterCriticalSection(&m_cs);
        int count = m_count;
        LeaveCriticalSection(&m_cs);
        return count;
    }

    int GetWaitingThreads() {
        EnterCriticalSection(&m_cs);
        int waiting = m_waitingThreads;
        LeaveCriticalSection(&m_cs);
        return waiting;
    }
};

// WinAPI объекты синхронизации
HANDLE g_mutex;
HANDLE g_semaphore_empty;
HANDLE g_semaphore_full;
HANDLE g_calculation_complete;
CRITICAL_SECTION g_critical_section;
CRITICAL_SECTION g_console_section;

// Кастомные семафоры для демонстрации
CustomSemaphore g_custom_semaphore_empty(BUFFER_SIZE, BUFFER_SIZE);
CustomSemaphore g_custom_semaphore_full(0, BUFFER_SIZE);

// Структура для передачи данных в потоки
struct ThreadData {
    int thread_id;
    int start_index;
    int end_index;
    double result;
};

// Функция для заполнения данных
void InitializeData() {
    EnterCriticalSection(&g_console_section);
    cout << "Инициализация данных..." << endl;
    LeaveCriticalSection(&g_console_section);

    for (int i = 0; i < DATA_SIZE; i++) {
        g_data[i] = rand() % 100 + 1;
    }
}

// Функция вычисления (исправленная - без переполнения)
double HeavyCalculation(int value) {
    double result = 0.0;
    for (int i = 0; i < 50; i++) {
        result += log(value + i + 1) * cos(value) / (value + 1);
    }
    return result;
}

// Поток для вычислений с мьютексом
DWORD WINAPI CalculationThreadMutex(LPVOID lpParam) {
    ThreadData* data = (ThreadData*)lpParam;
    double local_sum = 0.0;

    EnterCriticalSection(&g_console_section);
    cout << "Поток " << data->thread_id << " начал вычисления ["
        << data->start_index << " - " << data->end_index << "]" << endl;
    LeaveCriticalSection(&g_console_section);

    for (int i = data->start_index; i < data->end_index; i++) {
        double temp_result = HeavyCalculation(g_data[i]);
        g_results[i] = temp_result;
        local_sum += temp_result;

        WaitForSingleObject(g_mutex, INFINITE);
        data->result += temp_result;
        ReleaseMutex(g_mutex);
    }

    EnterCriticalSection(&g_console_section);
    cout << "Поток " << data->thread_id << " завершил вычисления. Локальная сумма: " << local_sum << endl;
    LeaveCriticalSection(&g_console_section);

    return 0;
}

// Производитель для демонстрации WinAPI семафоров
DWORD WINAPI ProducerThread(LPVOID lpParam) {
    int thread_id = *(int*)lpParam;

    for (int i = 0; i < 5; i++) {
        int item = thread_id * 100 + i;

        WaitForSingleObject(g_semaphore_empty, INFINITE);

        WaitForSingleObject(g_mutex, INFINITE);
        g_buffer[g_buffer_index] = item;

        EnterCriticalSection(&g_console_section);
        cout << "Производитель " << thread_id << " добавил: " << item
            << " в позицию " << g_buffer_index << endl;
        LeaveCriticalSection(&g_console_section);

        g_buffer_index++;
        ReleaseMutex(g_mutex);

        ReleaseSemaphore(g_semaphore_full, 1, NULL);

        Sleep(200);
    }

    EnterCriticalSection(&g_console_section);
    cout << "Производитель " << thread_id << " завершил работу" << endl;
    LeaveCriticalSection(&g_console_section);

    return 0;
}

// Потребитель для демонстрации WinAPI семафоров
DWORD WINAPI ConsumerThread(LPVOID lpParam) {
    int thread_id = *(int*)lpParam;
    int consumed_count = 0;

    while (consumed_count < 3) {
        WaitForSingleObject(g_semaphore_full, INFINITE);

        WaitForSingleObject(g_mutex, INFINITE);
        if (g_buffer_index > 0) {
            g_buffer_index--;
            int item = g_buffer[g_buffer_index];

            EnterCriticalSection(&g_console_section);
            cout << "Потребитель " << thread_id << " забрал: " << item
                << " из позиции " << g_buffer_index << endl;
            LeaveCriticalSection(&g_console_section);

            consumed_count++;
        }
        ReleaseMutex(g_mutex);

        ReleaseSemaphore(g_semaphore_empty, 1, NULL);

        Sleep(300);
    }

    EnterCriticalSection(&g_console_section);
    cout << "Потребитель " << thread_id << " завершил работу" << endl;
    LeaveCriticalSection(&g_console_section);

    return 0;
}

// Производитель для демонстрации КАСТОМНЫХ семафоров
DWORD WINAPI ProducerThreadCustom(LPVOID lpParam) {
    int thread_id = *(int*)lpParam;

    for (int i = 0; i < 6; i++) {
        int item = thread_id * 100 + i;

        EnterCriticalSection(&g_console_section);
        cout << "[КАСТОМ] Производитель " << thread_id << " ЖДЕТ свободного места..."
            << " (свободно: " << g_custom_semaphore_empty.GetCount()
            << ", ждут: " << g_custom_semaphore_empty.GetWaitingThreads() << ")" << endl;
        LeaveCriticalSection(&g_console_section);

        g_custom_semaphore_empty.Wait();

        EnterCriticalSection(&g_critical_section);
        g_buffer[g_buffer_index] = item;

        EnterCriticalSection(&g_console_section);
        cout << "[КАСТОМ] >>> Производитель " << thread_id << " добавил: " << item
            << " в позицию " << g_buffer_index
            << " (данных теперь: " << g_custom_semaphore_full.GetCount() + 1 << ")" << endl;
        LeaveCriticalSection(&g_console_section);

        g_buffer_index++;
        LeaveCriticalSection(&g_critical_section);

        g_custom_semaphore_full.Release();

        Sleep(150);
    }

    EnterCriticalSection(&g_console_section);
    cout << "[КАСТОМ] Производитель " << thread_id << " завершил работу" << endl;
    LeaveCriticalSection(&g_console_section);

    return 0;
}

// Потребитель для демонстрации КАСТОМНЫХ семафоров
DWORD WINAPI ConsumerThreadCustom(LPVOID lpParam) {
    int thread_id = *(int*)lpParam;

    for (int i = 0; i < 4; i++) {
        EnterCriticalSection(&g_console_section);
        cout << "[КАСТОМ] Потребитель " << thread_id << " ЖДЕТ данных..."
            << " (данных: " << g_custom_semaphore_full.GetCount()
            << ", ждут: " << g_custom_semaphore_full.GetWaitingThreads() << ")" << endl;
        LeaveCriticalSection(&g_console_section);

        g_custom_semaphore_full.Wait();

        EnterCriticalSection(&g_critical_section);
        if (g_buffer_index > 0) {
            g_buffer_index--;
            int item = g_buffer[g_buffer_index];

            EnterCriticalSection(&g_console_section);
            cout << "[КАСТОМ] <<< Потребитель " << thread_id << " забрал: " << item
                << " из позиции " << g_buffer_index
                << " (свободно теперь: " << g_custom_semaphore_empty.GetCount() + 1 << ")" << endl;
            LeaveCriticalSection(&g_console_section);
        }
        LeaveCriticalSection(&g_critical_section);

        g_custom_semaphore_empty.Release();

        Sleep(200);
    }

    EnterCriticalSection(&g_console_section);
    cout << "[КАСТОМ] Потребитель " << thread_id << " завершил работу" << endl;
    LeaveCriticalSection(&g_console_section);

    return 0;
}

// Поток для поиска простых чисел
DWORD WINAPI PrimeCalculatorThread(LPVOID lpParam) {
    ThreadData* data = (ThreadData*)lpParam;
    vector<int> local_primes;

    EnterCriticalSection(&g_console_section);
    cout << "Поток " << data->thread_id << " ищет простые числа в диапазоне ["
        << data->start_index << " - " << data->end_index << "]" << endl;
    LeaveCriticalSection(&g_console_section);

    for (int num = data->start_index; num <= data->end_index; num++) {
        if (num < 2) continue;

        bool is_prime = true;
        for (int i = 2; i * i <= num; i++) {
            if (num % i == 0) {
                is_prime = false;
                break;
            }
        }

        if (is_prime) {
            local_primes.push_back(num);
        }

        if (WaitForSingleObject(g_calculation_complete, 0) == WAIT_OBJECT_0) {
            EnterCriticalSection(&g_console_section);
            cout << "Поток " << data->thread_id << " прерван по запросу пользователя" << endl;
            LeaveCriticalSection(&g_console_section);
            break;
        }
    }

    EnterCriticalSection(&g_critical_section);
    cout << "Поток " << data->thread_id << " нашел " << local_primes.size()
        << " простых чисел.";
    if (!local_primes.empty()) {
        cout << " Последние 3: ";
        for (int i = max(0, (int)local_primes.size() - 3); i < local_primes.size(); i++) {
            cout << local_primes[i] << " ";
        }
    }
    cout << endl;
    LeaveCriticalSection(&g_critical_section);

    return 0;
}

// Демонстрация вычислений с мьютексами
void DemoMutexCalculations() {
    EnterCriticalSection(&g_console_section);
    cout << "\n=== Демонстрация вычислений с мьютексами ===" << endl;
    LeaveCriticalSection(&g_console_section);

    HANDLE threads[THREAD_COUNT];
    ThreadData thread_data[THREAD_COUNT];
    int chunk_size = DATA_SIZE / THREAD_COUNT;

    for (int i = 0; i < THREAD_COUNT; i++) {
        thread_data[i].thread_id = i + 1;
        thread_data[i].start_index = i * chunk_size;
        thread_data[i].end_index = (i == THREAD_COUNT - 1) ? DATA_SIZE : (i + 1) * chunk_size;
        thread_data[i].result = 0.0;

        threads[i] = CreateThread(NULL, 0, CalculationThreadMutex, &thread_data[i], 0, NULL);
    }

    WaitForMultipleObjects(THREAD_COUNT, threads, TRUE, INFINITE);

    double total_sum = 0.0;
    for (int i = 0; i < THREAD_COUNT; i++) {
        total_sum += thread_data[i].result;
        CloseHandle(threads[i]);
    }

    EnterCriticalSection(&g_console_section);
    cout << "Общая сумма вычислений: " << total_sum << endl;
    cout << "Среднее значение: " << total_sum / DATA_SIZE << endl;
    LeaveCriticalSection(&g_console_section);
}

// Демонстрация Producer-Consumer с WinAPI семафорами
void DemoSemaphores() {
    EnterCriticalSection(&g_console_section);
    cout << "\n=== Демонстрация Producer-Consumer с WinAPI семафорами ===" << endl;
    LeaveCriticalSection(&g_console_section);

    g_buffer_index = 0;
    fill(g_buffer.begin(), g_buffer.end(), 0);

    HANDLE producers[2], consumers[2];
    int producer_ids[2] = { 1, 2 };
    int consumer_ids[2] = { 1, 2 };

    for (int i = 0; i < 2; i++) {
        producers[i] = CreateThread(NULL, 0, ProducerThread, &producer_ids[i], 0, NULL);
    }

    for (int i = 0; i < 2; i++) {
        consumers[i] = CreateThread(NULL, 0, ConsumerThread, &consumer_ids[i], 0, NULL);
    }

    WaitForMultipleObjects(2, consumers, TRUE, INFINITE);

    for (int i = 0; i < 2; i++) {
        TerminateThread(producers[i], 0);
        CloseHandle(producers[i]);
        CloseHandle(consumers[i]);
    }

    EnterCriticalSection(&g_console_section);
    cout << "Демонстрация WinAPI семафоров завершена" << endl;
    LeaveCriticalSection(&g_console_section);
}

// Демонстрация Producer-Consumer с КАСТОМНЫМИ семафорами
void DemoCustomSemaphores() {
    EnterCriticalSection(&g_console_section);
    cout << "\n=== Демонстрация Producer-Consumer с КАСТОМНЫМИ семафорами ===" << endl;
    cout << "Семафоры реализованы на базе критических секций" << endl;
    LeaveCriticalSection(&g_console_section);

    g_buffer_index = 0;
    fill(g_buffer.begin(), g_buffer.end(), 0);

    HANDLE producers[2], consumers[2];
    int producer_ids[2] = { 1, 2 };
    int consumer_ids[2] = { 1, 2 };

    for (int i = 0; i < 2; i++) {
        producers[i] = CreateThread(NULL, 0, ProducerThreadCustom, &producer_ids[i], 0, NULL);
    }

    for (int i = 0; i < 2; i++) {
        consumers[i] = CreateThread(NULL, 0, ConsumerThreadCustom, &consumer_ids[i], 0, NULL);
    }

    WaitForMultipleObjects(2, consumers, TRUE, INFINITE);

    for (int i = 0; i < 2; i++) {
        TerminateThread(producers[i], 0);
        CloseHandle(producers[i]);
        CloseHandle(consumers[i]);
    }

    EnterCriticalSection(&g_console_section);
    cout << "Демонстрация кастомных семафоров завершена" << endl;
    LeaveCriticalSection(&g_console_section);
}

// Демонстрация поиска простых чисел с возможностью прерывания
void DemoPrimeCalculation() {
    EnterCriticalSection(&g_console_section);
    cout << "\n=== Демонстрация поиска простых чисел ===" << endl;
    cout << "Нажмите Enter для прерывания вычислений..." << endl;
    LeaveCriticalSection(&g_console_section);

    ResetEvent(g_calculation_complete);

    HANDLE threads[THREAD_COUNT];
    ThreadData thread_data[THREAD_COUNT];
    int range_start = 100000;
    int range_end = 200000;
    int range_chunk = (range_end - range_start) / THREAD_COUNT;

    for (int i = 0; i < THREAD_COUNT; i++) {
        thread_data[i].thread_id = i + 1;
        thread_data[i].start_index = range_start + i * range_chunk;
        thread_data[i].end_index = (i == THREAD_COUNT - 1) ? range_end : range_start + (i + 1) * range_chunk;

        threads[i] = CreateThread(NULL, 0, PrimeCalculatorThread, &thread_data[i], 0, NULL);
    }

    cin.ignore();
    cin.get();
    SetEvent(g_calculation_complete);

    WaitForMultipleObjects(THREAD_COUNT, threads, TRUE, INFINITE);

    for (int i = 0; i < THREAD_COUNT; i++) {
        CloseHandle(threads[i]);
    }

    EnterCriticalSection(&g_console_section);
    cout << "Поиск простых чисел завершен" << endl;
    LeaveCriticalSection(&g_console_section);
}

int main() {
    setlocale(LC_ALL, "Russian");

    cout << "==================================================" << endl;
    cout << "Лабораторная работа: Потоки в Windows" << endl;
    cout << "Мьютексы, семафоры WinAPI и кастомные семафоры" << endl;
    cout << "==================================================" << endl;

    // Инициализация объектов синхронизации
    InitializeCriticalSection(&g_console_section);
    InitializeCriticalSection(&g_critical_section);
    g_mutex = CreateMutex(NULL, FALSE, NULL);
    g_semaphore_empty = CreateSemaphore(NULL, BUFFER_SIZE, BUFFER_SIZE, NULL);
    g_semaphore_full = CreateSemaphore(NULL, 0, BUFFER_SIZE, NULL);
    g_calculation_complete = CreateEvent(NULL, TRUE, FALSE, NULL);

    if (!g_mutex || !g_semaphore_empty || !g_semaphore_full || !g_calculation_complete) {
        cout << "Ошибка инициализации объектов синхронизации!" << endl;
        return 1;
    }

    InitializeData();

    int choice;
    do {
        cout << "\nМеню демонстраций:" << endl;
        cout << "1. Вычисления с мьютексами" << endl;
        cout << "2. Producer-Consumer с WinAPI семафорами" << endl;
        cout << "3. Producer-Consumer с КАСТОМНЫМИ семафорами" << endl;
        cout << "4. Поиск простых чисел (с прерыванием)" << endl;
        cout << "5. Все демонстрации последовательно" << endl;
        cout << "0. Выход" << endl;
        cout << "Выберите опцию: ";
        cin >> choice;

        switch (choice) {
        case 1:
            DemoMutexCalculations();
            break;
        case 2:
            DemoSemaphores();
            break;
        case 3:
            DemoCustomSemaphores();
            break;
        case 4:
            DemoPrimeCalculation();
            break;
        case 5:
            DemoMutexCalculations();
            DemoSemaphores();
            DemoCustomSemaphores();
            DemoPrimeCalculation();
            break;
        case 0:
            cout << "Завершение работы..." << endl;
            break;
        default:
            cout << "Неверный выбор!" << endl;
        }
    } while (choice != 0);

    // Очистка ресурсов
    DeleteCriticalSection(&g_console_section);
    DeleteCriticalSection(&g_critical_section);
    CloseHandle(g_mutex);
    CloseHandle(g_semaphore_empty);
    CloseHandle(g_semaphore_full);
    CloseHandle(g_calculation_complete);

    return 0;
}