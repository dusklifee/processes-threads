#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <chrono>
#include <iomanip>

using namespace std;

// --- CUSTOM SEMAPHORE CLASS ---
// Implements a semaphore using a Mutex and a Condition Variable (Standard POSIX pattern)
class CustomSemaphore {
private:
    mutex mtx;
    condition_variable cv;
    int count;
    int max_permits;

public:
    // Initial count defines how many threads can enter the critical section simultaneously
    CustomSemaphore(int init) : count(init), max_permits(init) {}

    // Wait operation (P-operation / Acquire)
    void wait(int thread_id) {
        unique_lock<mutex> lock(mtx);

        // If count is 0, the thread goes to sleep until someone calls post()
        // The lambda [this]{ return count > 0; } protects against "spurious wakeups"
        cv.wait(lock, [this] { return count > 0; });

        --count;
        cout << "[Semaphore] Permit GRANTED to Thread " << thread_id
            << " (Available: " << count << "/" << max_permits << ")" << endl;
    }

    // Post operation (V-operation / Release)
    void post(int thread_id) {
        unique_lock<mutex> lock(mtx);
        ++count;
        cout << "[Semaphore] Permit RELEASED by Thread " << thread_id
            << " (Available: " << count << "/" << max_permits << ")" << endl;

        // Notify one waiting thread that a permit is available
        cv.notify_one();
    }
};

// --- GLOBAL RESOURCES ---
long long shared_counter = 0;   // The resource being protected by Mutex
mutex counter_mutex;            // Mutex to prevent Race Condition
CustomSemaphore sem(3);         // Semaphore: limits concurrency to 3 threads

// --- WORKER THREAD FUNCTION ---
void worker(int id, int iterations) {
    cout << "[Thread " << id << "] STATUS: Waiting in queue..." << endl;

    // 1. Acquire semaphore permit
    sem.wait(id);

    // --- CRITICAL SECTION (PERMIT GRANTED) ---
    auto start_work = chrono::high_resolution_clock::now();
    cout << "[Thread " << id << "] >>> ACTIVE: Incrementing shared counter..." << endl;

    for (int i = 0; i < iterations; ++i) {
        // 2. Protect individual increment with a Mutex (RAII wrapper)
        lock_guard<mutex> lock(counter_mutex);
        shared_counter++;
    }

    // Simulate some payload / heavy processing
    this_thread::sleep_for(chrono::milliseconds(1000));

    auto end_work = chrono::high_resolution_clock::now();
    // --- END OF CRITICAL SECTION ---

    // 3. Release semaphore permit for the next thread
    sem.post(id);

    chrono::duration<double> work_duration = end_work - start_work;
    cout << "[Thread " << id << "] <<< FINISHED. Time inside: " << fixed << setprecision(3)
        << work_duration.count() << "s" << endl;
}

int main() {
    const int total_threads = 8;        // Total number of threads to spawn
    const int iters_per_thread = 500000; // Each thread performs 0.5M increments

    cout << "==================================================" << endl;
    cout << "   CUSTOM SEMAPHORE & MUTEX PERFORMANCE LAB" << endl;
    cout << "==================================================" << endl;
    cout << "Total Threads Spawned      : " << total_threads << endl;
    cout << "Semaphore Capacity         : 3" << endl;
    cout << "Expected Final Counter     : " << (long long)total_threads * iters_per_thread << endl;
    cout << "--------------------------------------------------" << endl;

    auto start_total = chrono::high_resolution_clock::now();

    // Create and start threads
    vector<thread> threads;
    for (int i = 1; i <= total_threads; ++i) {
        threads.emplace_back(worker, i, iters_per_thread);
    }

    // Wait for all threads to finish
    for (auto& t : threads) {
        t.join();
    }

    auto end_total = chrono::high_resolution_clock::now();

    // Calculate final metrics
    chrono::duration<double> total_duration = end_total - start_total;
    double total_ops = (double)total_threads * iters_per_thread;
    double throughput = total_ops / total_duration.count();

    cout << "--------------------------------------------------" << endl;
    cout << "FINAL RESULTS:" << endl;
    cout << "Shared Counter Value       : " << shared_counter << endl;
    cout << "Total Execution Time       : " << total_duration.count() << " seconds" << endl;
    cout << "Average Throughput         : " << fixed << setprecision(0) << throughput << " ops/sec" << endl;

    // Safety check
    if (shared_counter == (long long)total_threads * iters_per_thread) {
        cout << "SYSTEM STATUS              : SUCCESS (No Data Loss)" << endl;
    }
    else {
        cout << "SYSTEM STATUS              : FAILED (Race Condition Detected!)" << endl;
    }
    cout << "==================================================" << endl;

    return 0;
}
