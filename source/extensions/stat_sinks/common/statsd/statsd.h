#pragma once

#include "envoy/local_info/local_info.h"
#include "envoy/network/connection.h"
#include "envoy/stats/stats.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/buffer/buffer_impl.h"

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace Common {
namespace Statsd {

static const std::string DEFAULT_PREFIX = "envoy";
/**
 * This is a simple UDP localhost writer for statsd messages.
 */
class Writer : public ThreadLocal::ThreadLocalObject {
public:
  Writer(Network::Address::InstanceConstSharedPtr address);
  // For testing.
  Writer() : fd_(-1) {}
  virtual ~Writer();

  virtual void write(const std::string& message);
  // Called in unit test to validate address.
  int getFdForTests() const { return fd_; };

private:
  int fd_;
};

/**
 * Implementation of Sink that writes to a UDP statsd address.
 */
class UdpStatsdSink : public Stats::Sink {
public:
  UdpStatsdSink(ThreadLocal::SlotAllocator& tls, Network::Address::InstanceConstSharedPtr address,
                const bool use_tag, const std::string _prefix = DEFAULT_PREFIX);
  // For testing.
  UdpStatsdSink(ThreadLocal::SlotAllocator& tls, const std::shared_ptr<Writer>& writer,
                const bool use_tag, const std::string _prefix = DEFAULT_PREFIX)
      : tls_(tls.allocateSlot()), use_tag_(use_tag) {
    tls_->set(
        [writer](Event::Dispatcher&) -> ThreadLocal::ThreadLocalObjectSharedPtr { return writer; });
    if (_prefix.size() != 0) {
      prefix = _prefix;
    }
  }

  // Stats::Sink
  void beginFlush() override {}
  void flushCounter(const Stats::Counter& counter, uint64_t delta) override;
  void flushGauge(const Stats::Gauge& gauge, uint64_t value) override;
  void endFlush() override {}
  void onHistogramComplete(const Stats::Histogram& histogram, uint64_t value) override;

  // Called in unit test to validate writer construction and address.
  int getFdForTests() { return tls_->getTyped<Writer>().getFdForTests(); }
  bool getUseTagForTest() { return use_tag_; }
  std::string getPrefix() { return prefix; }

private:
  const std::string getName(const Stats::Metric& metric);
  const std::string buildTagStr(const std::vector<Stats::Tag>& tags);

  ThreadLocal::SlotPtr tls_;
  Network::Address::InstanceConstSharedPtr server_address_;
  const bool use_tag_;
  // Prefix for all flushed stats.
  std::string prefix;
};

/**
 * Per thread implementation of a TCP stats flusher for statsd.
 */
class TcpStatsdSink : public Stats::Sink {
public:
  TcpStatsdSink(const LocalInfo::LocalInfo& local_info, const std::string& cluster_name,
                ThreadLocal::SlotAllocator& tls, Upstream::ClusterManager& cluster_manager,
                Stats::Scope& scope, std::string _prefix = DEFAULT_PREFIX);

  // Stats::Sink
  void beginFlush() override { tls_->getTyped<TlsSink>().beginFlush(true); }

  void flushCounter(const Stats::Counter& counter, uint64_t delta) override {
    tls_->getTyped<TlsSink>().flushCounter(counter.name(), delta);
  }

  void flushGauge(const Stats::Gauge& gauge, uint64_t value) override {
    tls_->getTyped<TlsSink>().flushGauge(gauge.name(), value);
  }

  void endFlush() override { tls_->getTyped<TlsSink>().endFlush(true); }

  void onHistogramComplete(const Stats::Histogram& histogram, uint64_t value) override {
    // For statsd histograms are all timers.
    tls_->getTyped<TlsSink>().onTimespanComplete(histogram.name(),
                                                 std::chrono::milliseconds(value));
  }

  std::string getPrefix() { return prefix; }

private:
  struct TlsSink : public ThreadLocal::ThreadLocalObject, public Network::ConnectionCallbacks {
    TlsSink(TcpStatsdSink& parent, Event::Dispatcher& dispatcher);
    ~TlsSink();

    void beginFlush(bool expect_empty_buffer);
    void checkSize();
    void commonFlush(const std::string& name, uint64_t value, char stat_type);
    void flushCounter(const std::string& name, uint64_t delta);
    void flushGauge(const std::string& name, uint64_t value);
    void endFlush(bool do_write);
    void onTimespanComplete(const std::string& name, std::chrono::milliseconds ms);
    uint64_t usedBuffer();
    void write(Buffer::Instance& buffer);

    // Network::ConnectionCallbacks
    void onEvent(Network::ConnectionEvent event) override;
    void onAboveWriteBufferHighWatermark() override {}
    void onBelowWriteBufferLowWatermark() override {}

    TcpStatsdSink& parent_;
    Event::Dispatcher& dispatcher_;
    Network::ClientConnectionPtr connection_;
    Buffer::OwnedImpl buffer_;
    Buffer::RawSlice current_buffer_slice_;
    char* current_slice_mem_{};
  };

  // Somewhat arbitrary 16MiB limit for buffered stats.
  static constexpr uint32_t MAX_BUFFERED_STATS_BYTES = (1024 * 1024 * 16);

  // 16KiB intermediate buffer for flushing.
  static constexpr uint32_t FLUSH_SLICE_SIZE_BYTES = (1024 * 16);

  // Prefix for all flushed stats.
  std::string prefix;

  Upstream::ClusterInfoConstSharedPtr cluster_info_;
  ThreadLocal::SlotPtr tls_;
  Upstream::ClusterManager& cluster_manager_;
  Stats::Counter& cx_overflow_stat_;
};

} // namespace Statsd
} // namespace Common
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
