#ifndef __DUMMY_ENGINE_HH__
#define __DUMMY_ENGINE_HH__

#include <nba/framework/computedevice.hh>
#include <nba/framework/computecontext.hh>
#include <nba/core/threading.hh>
#include <nba/engines/dummy/computecontext.hh>
#include <string>
#include <vector>
#include <deque>

namespace nba
{

class DummyComputeDevice: public ComputeDevice
{
public:
    friend class DummyComputeContext;

    DummyComputeDevice(unsigned node_id, unsigned device_id, size_t num_contexts);
    virtual ~DummyComputeDevice();

    int get_spec(struct compute_device_spec *spec);
    int get_utilization(struct compute_device_util *util);
    void *alloc_host_buffer(size_t size, int flags);
    memory_t alloc_device_buffer(size_t size, int flags);
    void free_host_buffer(void *ptr);
    void free_device_buffer(memory_t ptr);
    void memwrite(void *host_buf, memory_t dev_buf, size_t offset, size_t size);
    void memread(void *host_buf, memory_t dev_buf, size_t offset, size_t size);

private:
    ComputeContext *_get_available_context();
    void _return_context(ComputeContext *ctx);

    std::deque<DummyComputeContext *> _ready_contexts;
    std::deque<DummyComputeContext *> _active_contexts;
    Lock _lock;
    CondVar _ready_cond;
};

}

#endif

// vim: ts=8 sts=4 sw=4 et
