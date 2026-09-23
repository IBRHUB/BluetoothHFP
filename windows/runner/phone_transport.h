#ifndef RUNNER_PHONE_TRANSPORT_H_
#define RUNNER_PHONE_TRANSPORT_H_

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <chrono>

struct TransportStatus {
  bool media_open = false;
  bool calls_connected = false;
  std::string media_state = "idle";
  std::string calls_state = "idle";
  std::chrono::steady_clock::time_point media_since{};
  std::chrono::steady_clock::time_point calls_since{};
  std::wstring media_message = L"Select an iPhone to receive media.";
  std::wstring calls_message = L"Select an iPhone to connect calls.";
};

// All WinRT work lives on one MTA thread. Requests supersede pending operations;
// no callbacks retain the Flutter window or touch UI-owned state.
class PhoneTransport {
 public:
  PhoneTransport();
  ~PhoneTransport();
  void Select(const std::wstring& address);
  TransportStatus Status() const;
  void Stop();

 private:
  void Run();
  mutable std::mutex mutex_;
  std::condition_variable wake_;
  std::wstring address_;
  TransportStatus status_;
  std::atomic<unsigned long> revision_{0};
  std::atomic<bool> stop_{false};
  std::thread worker_;
};

#endif
