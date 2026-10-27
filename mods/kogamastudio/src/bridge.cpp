#include "bridge.h"
#include <deque>
#include <mutex>

namespace bridge {
    namespace {
        std::mutex lock;
        std::deque<std::string> forGame;
        std::deque<std::string> forMenu;

        bool Take(std::deque<std::string>& queue, std::string& out) {
            std::lock_guard<std::mutex> guard(lock);
            if (queue.empty()) return false;
            out = std::move(queue.front());
            queue.pop_front();
            return true;
        }
    }

    void ToGame(const std::string& command) {
        std::lock_guard<std::mutex> guard(lock);
        forGame.push_back(command);
    }

    bool NextForGame(std::string& command) { return Take(forGame, command); }

    void ToMenu(const std::string& message) {
        std::lock_guard<std::mutex> guard(lock);
        forMenu.push_back(message);
    }

    bool NextForMenu(std::string& message) { return Take(forMenu, message); }
}
