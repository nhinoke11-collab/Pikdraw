#include "pikdraw/document/document.hpp"
#include <windows.h>
#include <psapi.h>
#include <chrono>
#include <iostream>
#include <algorithm>

using namespace pikdraw::document;
using namespace pikdraw::geometry;
using Clock=std::chrono::steady_clock;

class Populate final : public DocumentCommand {
public:
    void execute(Document& doc) override {
        for (int i=0;i<20000;++i) {
            auto object=std::make_shared<PathObject>(); object->id=doc.allocate_id();
            doc.internal_put_object(object);
        }
        auto large=std::make_shared<PathObject>(); large->id=doc.allocate_id();
        large->path.move_to({20000,0});
        for (int i=0;i<100000;++i) large->path.line_to({20000+i*0.001,static_cast<double>(i%13)});
        doc.internal_put_object(large);
    }
    void undo(Document&) override { throw std::logic_error("Benchmark uses snapshot-based history"); }
    std::string description() const override { return "Benchmark population"; }
};

SIZE_T private_bytes() {
    PROCESS_MEMORY_COUNTERS_EX memory{}; memory.cb=sizeof(memory);
    if (!GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),sizeof(memory)))
        throw std::runtime_error("Cannot measure process memory");
    return memory.PrivateUsage;
}

int main() {
    try {
        const auto initial_memory=private_bytes();
        auto doc=std::make_unique<Document>("performance-fixture");
        auto started=Clock::now(); doc->execute_command(std::make_unique<Populate>());
        const auto population_ms=std::chrono::duration<double,std::milli>(Clock::now()-started).count();
        const auto loaded_memory=private_bytes();
        std::vector<double> samples;
#ifdef PIKDRAW_BENCHMARK_PHASES
        std::vector<TransactionMetrics> phases;
#endif
        std::vector<std::shared_ptr<const DocumentSnapshot>> retained;
        retained.push_back(doc->create_snapshot());
        for (int i=0;i<64;++i) {
            started=Clock::now();
            doc->execute_command(std::make_unique<TransformObjectCommand>(20001,Transform2D::translation(0.125,-0.25)));
            samples.push_back(std::chrono::duration<double,std::milli>(Clock::now()-started).count());
#ifdef PIKDRAW_BENCHMARK_PHASES
            phases.push_back(doc->last_transaction_metrics());
#endif
            retained.push_back(doc->create_snapshot());
        }
        if (doc->create_snapshot()->object_count()!=20001 || doc->revision()!=65)
            throw std::runtime_error("Benchmark command count mismatch");
        for (size_t i=0;i<retained.size();++i) {
            const auto object=retained[i]->get_object(20001);
            if (!object || object->transform.tx!=i*0.125 || object->transform.ty!=-static_cast<double>(i)*0.25)
                throw std::runtime_error("Retained geometry changed");
            const auto& bounds=retained[i]->spatial_index().at(20001);
            if (bounds.min_x!=20000+i*0.125) throw std::runtime_error("Retained index mismatch");
        }
        const auto retained_memory=private_bytes();
        std::weak_ptr<const DocumentSnapshot> probe=retained.front();
        started=Clock::now(); retained.clear(); doc.reset();
        const auto cleanup_ms=std::chrono::duration<double,std::milli>(Clock::now()-started).count();
        if (!probe.expired()) throw std::runtime_error("History/root lifetime leak");
        std::sort(samples.begin(),samples.end());
        std::cout << "{\"fixture\":\"20000 small objects + 100001-segment path; 64 transforms; 65 retained snapshots\","
                  << "\"configuration\":\"Release O2; no renderer/UI\",\"population_ms\":" << population_ms
                  << ",\"command_p50_ms\":" << samples[samples.size()/2]
                  << ",\"command_p95_ms\":" << samples[static_cast<size_t>(samples.size()*0.95)]
                  << ",\"command_max_ms\":" << samples.back()
                  << ",\"private_initial_bytes\":" << initial_memory << ",\"private_loaded_bytes\":" << loaded_memory
                  << ",\"private_retained_bytes\":" << retained_memory
                  << ",\"private_after_cleanup_bytes\":" << private_bytes()
                  << ",\"cleanup_ms\":" << cleanup_ms << ",\"root_expired\":true,\"full_spike_accepted\":false,\"samples_sorted_ms\":[";
        for (size_t i=0;i<samples.size();++i) { if (i) std::cout << ','; std::cout << samples[i]; }
        std::cout << "],\"phase_samples_us\":[";
#ifdef PIKDRAW_BENCHMARK_PHASES
        for (size_t i=0;i<phases.size();++i) {
            if (i) std::cout << ',';
            const auto& p=phases[i];
            std::cout << "{\"prepare\":" << p.prepare_us << ",\"operation_including_validation\":" << p.operation_us
                      << ",\"source_validation\":" << p.source_validation_us << ",\"snapshot\":" << p.snapshot_us
                      << ",\"history_cleanup\":" << p.history_cleanup_us << ",\"publish\":" << p.publish_us
                      << ",\"total\":" << p.total_us << '}';
        }
#endif
        std::cout << "]}\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
