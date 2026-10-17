#include "pikdraw/workers/process_launcher.hpp"
#include "worker_location.hpp"
#include <filesystem>
#include <iostream>
#include <securityappcontainer.h>
#include <stdexcept>

using namespace pikdraw::workers;
int main() {
  try {
    namespace fs = std::filesystem;
    const auto suffix = std::to_wstring(GetCurrentProcessId()) + L"." +
                        std::to_wstring(GetTickCount64());
    const auto scratch = fs::current_path() / (L"production-policy-" + suffix);
    const auto bin = scratch / L"bin";
    const auto executable = bin / L"spike_worker.exe";
    if (!fs::create_directory(scratch) || !fs::create_directory(bin))
      throw std::runtime_error("Cannot create fresh production policy fixture");
    const auto profile = L"PikDraw.Policy." + suffix;
    PSID raw_sid = nullptr;
    if (FAILED(DeriveAppContainerSidFromAppContainerName(profile.c_str(),
                                                         &raw_sid)) ||
        !raw_sid)
      throw std::runtime_error("Cannot derive the unique production probe SID");
    ScopedSid sid(raw_sid);
    if (!AppContainerProfileGuard::GrantDirectoryAccess(bin.wstring(),
                                                        sid.Get()) ||
        !fs::copy_file(WorkerProductionExecutable(), executable))
      throw std::runtime_error(
          "Cannot provision only the owned worker directory");
    WorkerLaunchConfig config;
    config.worker_executable_path = executable.wstring();
    config.working_directory = bin.wstring();
    config.appcontainer_profile_name = profile;
    WorkerProcessHost host;
    const bool launched = host.Launch(config);
    if (!launched || !host.GetAppContainer().IsValid())
      throw std::runtime_error("Production host cannot launch an isolated worker");
    pikdraw::interop::IpcHeader ping{};
    ping.message_type =
        static_cast<uint16_t>(pikdraw::interop::MessageType::Ping);
    ping.sequence_id = 1;
    const auto frame =
        std::span(reinterpret_cast<const uint8_t *>(&ping), sizeof(ping));
    if (!host.TransactBinaryPayload(frame, std::chrono::seconds(2)))
      throw std::runtime_error(
          "Production worker did not actually answer ping");
    pikdraw::interop::CanaryPayloadRequest canary{1, "", "", 0, 0};
    if (host.TransactBinaryPayload(
            pikdraw::interop::SerializeCanaryRequest(canary, 2),
            std::chrono::seconds(2)))
      throw std::runtime_error(
          "Production host exposed the test-only canary service");
    if (host.IsAlive())
      throw std::runtime_error("Rejected production canary left the child alive");
    config.use_appcontainer = false;
    config.trusted_test_worker = true;
    if (host.Launch(config) || host.GetAppContainer().IsValid())
      throw std::runtime_error(
          "Production host accepted the test-only sandbox bypass");
    config.use_appcontainer = true;
    if (host.Launch(config) || host.GetAppContainer().IsValid())
      throw std::runtime_error(
          "Production host accepted a test flag or created a profile");
    config.trusted_test_worker = false;
    ScopedHandle extra(CreateEventW(nullptr, FALSE, FALSE, nullptr));
    if (!extra.IsValid())
      throw std::runtime_error("Cannot create policy probe handle");
    config.test_inherited_handle = extra.Get();
    if (host.Launch(config) || host.GetAppContainer().IsValid())
      throw std::runtime_error(
          "Production host accepted an extra inherited handle");
    if (!fs::remove(executable) || !fs::remove(bin) || !fs::remove(scratch))
      throw std::runtime_error("Cannot clean exact production fixture files");
    std::cout << "PASS production worker runs isolated, rejects test service "
                 "and sandbox bypasses\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
