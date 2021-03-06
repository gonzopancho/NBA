#include "LookupIP6Route.hh"
#ifdef USE_CUDA
#include "LookupIP6Route_kernel.hh"
#endif
#include <nba/element/annotation.hh>
#include <nba/element/nodelocalstorage.hh>
#include <nba/framework/threadcontext.hh>
#include <nba/framework/computedevice.hh>
#include <nba/framework/computecontext.hh>
#include <cstdio>
#include <cstdlib>
#include <cassert>
#include <cerrno>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/ip6.h>
#include <rte_ether.h>
#include "util_routing_v6.hh"

#ifdef USE_CUDA
#include <cuda.h>
#include <nba/engines/cuda/utils.hh>
#endif

using namespace std;
using namespace nba;

static uint64_t ntohll(uint64_t val)
{
        return ( (((val) >> 56) & 0x00000000000000ff) | (((val) >> 40) & 0x000000000000ff00) | \
                (((val) >> 24) & 0x0000000000ff0000) | (((val) >>  8) & 0x00000000ff000000) | \
                (((val) <<  8) & 0x000000ff00000000) | (((val) << 24) & 0x0000ff0000000000) | \
                (((val) << 40) & 0x00ff000000000000) | (((val) << 56) & 0xff00000000000000) );
}

LookupIP6Route::LookupIP6Route(): OffloadableElement()
{
    #ifdef USE_CUDA
    auto ch = [this](ComputeContext *ctx, struct resource_param *res) { this->cuda_compute_handler(ctx, res); };
    offload_compute_handlers.insert({{"cuda", ch},});
    auto ih = [this](ComputeDevice *dev) { this->cuda_init_handler(dev); };
    offload_init_handlers.insert({{"cuda", ih},});
    #endif

    num_tx_ports = 0;
    rr_port = 0;

    _table_ptr = NULL;
    _rwlock_ptr = NULL;
    d_tables = NULL;
    d_table_sizes = NULL;
}

int LookupIP6Route::initialize()
{
    // Called after coproc threads are initialized.

    /* Get routing table pointers from the node-local storage. */
    _table_ptr = (RoutingTableV6*)ctx->node_local_storage->get_alloc("ipv6_table");
    _rwlock_ptr = ctx->node_local_storage->get_rwlock("ipv6_table");

    /* Get GPU device pointers from the node-local storage. */
    d_tables      = ((memory_t **) ctx->node_local_storage->get_alloc("dev_tables"))[0];
    d_table_sizes = ((memory_t **) ctx->node_local_storage->get_alloc("dev_table_sizes"))[0];
    return 0;
}

int LookupIP6Route::initialize_global()
{
    // Generate table randomly..
    int seed = 7659243;
    int count = 200000;
    _original_table.from_random(seed, count);
    _original_table.build();

    return 0;
}

int LookupIP6Route::initialize_per_node()
{
    /* Storage for routing table. */
    ctx->node_local_storage->alloc("ipv6_table", sizeof(RoutingTableV6));
    RoutingTableV6 *table = (RoutingTableV6*)ctx->node_local_storage->get_alloc("ipv6_table");
    new (table) RoutingTableV6();

    // Copy table for the each node..
    _original_table.copy_to(table);

    /* Storage for device pointers. */
    ctx->node_local_storage->alloc("dev_tables", sizeof(Item*) * 128);
    ctx->node_local_storage->alloc("dev_table_sizes", sizeof(size_t) * 128);

    return 0;
}

int LookupIP6Route::configure(comp_thread_context *ctx, std::vector<std::string> &args)
{
    Element::configure(ctx, args);
    num_tx_ports = ctx->num_tx_ports;
    num_nodes = ctx->num_nodes;
    node_idx = ctx->loc.node_id;
    rr_port = 0;

    return 0;
}

/* The CPU version */
int LookupIP6Route::process(int input_port, Packet *pkt)
{
    struct ether_hdr *ethh = (struct ether_hdr *) pkt->data();
    struct ip6_hdr *ip6h   = (struct ip6_hdr *)(ethh + 1);
    uint128_t dest_addr;
    uint16_t lookup_result = 0xffff;
    memcpy(&dest_addr.u64[1], &ip6h->ip6_dst.s6_addr32[0], sizeof(uint64_t));
    memcpy(&dest_addr.u64[0], &ip6h->ip6_dst.s6_addr32[2], sizeof(uint64_t));
    dest_addr.u64[1] = ntohll(dest_addr.u64[1]);
    dest_addr.u64[0] = ntohll(dest_addr.u64[0]);

    // TODO: make an interface to set these locks to be
    // automatically handled by process_batch() method.
    //rte_rwlock_read_lock(_rwlock_ptr);
    lookup_result = _table_ptr->lookup((reinterpret_cast<uint128_t*>(&dest_addr)));
    //rte_rwlock_read_unlock(_rwlock_ptr);

    if (lookup_result == 0xffff) {
        /* Could not find destination. Use the second output for "error" packets. */
        pkt->kill();
        return 0;
    }

    rr_port = (rr_port + 1) % num_tx_ports;
    anno_set(&pkt->anno, NBA_ANNO_IFACE_OUT, rr_port);
    output(0).push(pkt);
    return 0;
}

int LookupIP6Route::postproc(int input_port, void *custom_output, Packet *pkt)
{
    uint16_t lookup_result = *((uint16_t *)custom_output);
    if (lookup_result == 0xffff) {
        /* Could not find destination. Use the second output for "error" packets. */
        pkt->kill();
        return 0;
    }
    rr_port = (rr_port + 1) % num_tx_ports;
    anno_set(&pkt->anno, NBA_ANNO_IFACE_OUT, rr_port);
    output(0).push(pkt);
    return 0;
}

size_t LookupIP6Route::get_desired_workgroup_size(const char *device_name) const
{
    #ifdef USE_CUDA
    if (!strcmp(device_name, "cuda"))
        return 256u;
    #endif
    #ifdef USE_PHI
    if (!strcmp(device_name, "phi"))
        return 256u; // TODO: confirm
    #endif
    return 256u;
}

#ifdef USE_CUDA
void LookupIP6Route::cuda_compute_handler(ComputeContext *cctx, struct resource_param *res)
{
    struct kernel_arg arg;
    arg = {(void *) &d_tables, sizeof(void *), alignof(void *)};
    cctx->push_kernel_arg(arg);
    arg = {(void *) &d_table_sizes, sizeof(void *), alignof(void *)};
    cctx->push_kernel_arg(arg);
    kernel_t kern;
    kern.ptr = ipv6_route_lookup_get_cuda_kernel();
    cctx->enqueue_kernel_launch(kern, res);
}

void LookupIP6Route::cuda_init_handler(ComputeDevice *device)
{
    size_t table_sizes[128];
    memory_t table_ptrs_in_d[128];

    memory_t new_d_tables      = /*(Item **)*/ device->alloc_device_buffer(sizeof(memory_t *)*128, HOST_TO_DEVICE);
    memory_t new_d_table_sizes = /*(size_t *)*/ device->alloc_device_buffer(sizeof(size_t)*128, HOST_TO_DEVICE);

    /* table_ptrs_in_d keeps track of the temporary host-side references to tables in
     * the device for initialization and copy.
     * d_tables is the actual device buffer to store pointers in table_ptrs_in_d. */
    for (int i = 0; i < 128; i++) {
        table_sizes[i] = _original_table.m_Tables[i]->m_TableSize;
        table_ptrs_in_d[i] = /*(Item *)*/ device->alloc_device_buffer(sizeof(Item) * table_sizes[i] * 2, HOST_TO_DEVICE);
        device->memwrite(_original_table.m_Tables[i]->m_Table, table_ptrs_in_d[i], 0, sizeof(Item) * table_sizes[i] * 2);
    }
    device->memwrite(table_ptrs_in_d, new_d_tables, 0, sizeof(Item*) * 128);
    device->memwrite(table_sizes, new_d_table_sizes, 0, sizeof(size_t) * 128);

    /* Store the device pointers for per-thread instances. */
    d_tables      = (memory_t *) ctx->node_local_storage->get_alloc("dev_tables");
    d_table_sizes = (memory_t *) ctx->node_local_storage->get_alloc("dev_table_sizes");
    (d_tables)[0]      = new_d_tables;
    (d_table_sizes)[0] = new_d_table_sizes;
}
#endif

// vim: ts=8 sts=4 sw=4 et
