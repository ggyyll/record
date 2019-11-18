#include "record.h"
#include <thread>
#include <chrono>

int main(int argc, const char *argv[])
{
    (void)argc;
    (void)argv;
    std::string filename = ":0.0";
    Record record(filename);
    record.InitEnv();
    std::thread t([&]() {
        record.Run();
    });
    std::this_thread::sleep_for(std::chrono::seconds(5));
    record.Stop();
    t.join();
    return 0;
}
