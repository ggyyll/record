#include "record.hpp"
#include <functional>
#include <csignal>

namespace
{
std::function<void(int)> shutdown_handler;
void signal_handler(int signal) { shutdown_handler(signal); }
}  // namespace

int main()
{
    std::signal(SIGTERM, signal_handler);
    std::signal(SIGKILL, signal_handler);
    std::signal(SIGINT, signal_handler);

    std::string url(":0.0");

    RecordScreen record(url);
    record.InitEnv();

    shutdown_handler = [& record = record]([[maybe_unused]] int signal) { record.Stop(); };

    record.Run();
}
