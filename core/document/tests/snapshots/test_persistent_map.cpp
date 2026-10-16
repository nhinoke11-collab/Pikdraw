#include "pikdraw/document/snapshots/persistent_map.hpp"
#include <iostream>
#include <map>
#include <random>
#include <stdexcept>

using namespace pikdraw::document;

void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

int main() {
    try {
        PersistentObjectMap root;
        std::map<ObjectId, std::string> oracle;
        std::mt19937 random(731);
        for (size_t step = 0; step < 3000; ++step) {
            const ObjectId id = 1 + random() % 200;
            if ((random() % 3) == 0) {
                root = root.remove(id);
                oracle.erase(id);
            } else {
                PathObject object;
                object.id = id;
                object.name = std::to_string(step);
                root = root.set(id, object);
                oracle[id] = object.name;
            }
            check(root.size() == oracle.size(), "map count disagrees with std::map oracle");
            auto expected = oracle.begin();
            for (const auto& [key, object] : root.all()) {
                check(expected != oracle.end() && key == expected->first, "in-order traversal broken");
                check(std::get<PathObject>(object).name == expected->second, "value changed");
                ++expected;
            }
            check(expected == oracle.end(), "traversal lost nodes");
            const auto view = root.all();
            auto actual_reverse = view.rbegin();
            for (auto it = oracle.rbegin(); it != oracle.rend(); ++it, ++actual_reverse)
                check((*actual_reverse).first == it->first, "reverse traversal broken");
            check(actual_reverse == view.rend(), "reverse traversal length");
        }
        PathObject large;
        large.id = 1000;
        for (size_t i = 0; i < 100000; ++i) large.path.line_to(static_cast<double>(i), 1);
        root = root.set(large.id, std::move(large));
        const auto retained = root;
        const auto* original = retained.lookup_ref(1000);
        PathObject small;
        small.id = 2000;
        root = root.set(2000, small);
        check(root.lookup_ref(1000) == original, "unchanged 100k-node geometry was copied");
        check(!retained.contains(2000), "retained root changed after insertion");
        root = root.remove(1000);
        check(!root.contains(1000) && retained.contains(1000), "retained root changed after erase");
        check(std::get<PathObject>(*original).path.size() == 100000, "retained asset lifetime broken");
        std::cout << "PASS 3000 randomized map edits + traversal oracle + 100k-node sharing/lifetime\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
