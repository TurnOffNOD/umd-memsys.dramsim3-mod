#include "dram_system.h"

#include <assert.h>



#ifdef GENERATE_TRACE
#include "../ext/fmt/src/format.h"
#endif  // GENERATE_TRACE

namespace dramcore {

// alternative way is to assign the id in constructor but this is less
// destructive
int BaseDRAMSystem::num_mems_ = 0;

BaseDRAMSystem::BaseDRAMSystem(const std::string &config_file,
                                   const std::string &output_dir,
                                   std::function<void(uint64_t)> read_callback,
                                   std::function<void(uint64_t)> write_callback)
    : read_callback_(read_callback),
      write_callback_(write_callback),
      clk_(0),
      last_req_clk_(0) {
    mem_sys_id_ = num_mems_;
    num_mems_ += 1;
    ptr_config_ = new Config(config_file, output_dir);
    ptr_timing_ = new Timing(*ptr_config_);
    ptr_stats_ = new Statistics(*ptr_config_);

#ifdef THERMAL
    ptr_thermCal_ = new ThermalCalculator(*ptr_config_, *ptr_stats_);
#endif

    if (mem_sys_id_ > 0) {
        // if there are more than one memory_systems then rename the output to
        // preven being overwritten
        RenameFileWithNumber(ptr_config_->stats_file, mem_sys_id_);
        RenameFileWithNumber(ptr_config_->epoch_stats_file, mem_sys_id_);
        RenameFileWithNumber(ptr_config_->stats_file_csv, mem_sys_id_);
        RenameFileWithNumber(ptr_config_->epoch_stats_file_csv, mem_sys_id_);
    }

    if (ptr_config_->output_level >= 0) {
        stats_file_.open(ptr_config_->stats_file);
        stats_file_csv_.open(ptr_config_->stats_file_csv);
    }

    if (ptr_config_->output_level >= 1) {
        epoch_stats_file_csv_.open(ptr_config_->epoch_stats_file);
        ptr_stats_->PrintStatsCSVHeader(epoch_stats_file_csv_);
    }

    if (ptr_config_->output_level >= 2) {
        histo_stats_file_csv_.open(ptr_config_->histo_stats_file_csv);
    }

    if (ptr_config_->output_level >= 3) {
        epoch_stats_file_.open(ptr_config_->epoch_stats_file);
    }

#ifdef GENERATE_TRACE
    std::string addr_trace_name(ptr_config_->output_prefix + "addr.trace");
    address_trace_.open(addr_trace_name);
#endif
}

BaseDRAMSystem::~BaseDRAMSystem() {
    delete (ptr_stats_);
    delete (ptr_timing_);
    delete (ptr_config_);
#ifdef THERMAL
    delete (ptr_thermCal_);
#endif  // THERMAL

    stats_file_.close();
    epoch_stats_file_.close();
    stats_file_csv_.close();
    epoch_stats_file_csv_.close();

#ifdef GENERATE_TRACE
    address_trace_.close();
#endif
}

void BaseDRAMSystem::PrintIntermediateStats() {
    if (ptr_config_->output_level >= 1) {
        ptr_stats_->PrintEpochStatsCSVFormat(epoch_stats_file_csv_);
    }

    if (ptr_config_->output_level >= 2) {
        ptr_stats_->PrintEpochHistoStatsCSVFormat(histo_stats_file_csv_);
    }

    if (ptr_config_->output_level >= 3) {
        epoch_stats_file_
            << "-----------------------------------------------------"
            << std::endl;
        epoch_stats_file_ << "Epoch stats from clock = "
                          << clk_ - ptr_config_->epoch_period << " to " << clk_
                          << std::endl;
        epoch_stats_file_
            << "-----------------------------------------------------"
            << std::endl;
        ptr_stats_->PrintEpochStats(epoch_stats_file_);
        epoch_stats_file_
            << "-----------------------------------------------------"
            << std::endl;
    }
    return;
}

void BaseDRAMSystem::PrintStats() {
    // update one last time before print
    ptr_stats_->PreEpochCompute(clk_);
    ptr_stats_->UpdateEpoch(clk_);
    std::cout << "-----------------------------------------------------"
              << std::endl;
    std::cout << "Printing final stats of JedecDRAMSystem " << mem_sys_id_
              << " -- " << std::endl;
    std::cout << "-----------------------------------------------------"
              << std::endl;
    std::cout << *ptr_stats_;
    std::cout << "-----------------------------------------------------"
              << std::endl;
    if (ptr_config_->output_level >= 0) {
        ptr_stats_->PrintStats(stats_file_);
        // had to print the header here instead of the constructor
        // because histogram headers are only known at the end
        ptr_stats_->PrintStatsCSVHeader(stats_file_csv_);
        ptr_stats_->PrintStatsCSVFormat(stats_file_csv_);
#ifdef THERMAL
        ptr_thermCal_->PrintFinalPT(clk_);
#endif  // THERMAL
    }
    return;
}

JedecDRAMSystem::JedecDRAMSystem(const std::string &config_file,
                           const std::string &output_dir,
                           std::function<void(uint64_t)> read_callback,
                           std::function<void(uint64_t)> write_callback)
    : BaseDRAMSystem(config_file, output_dir, read_callback, write_callback) {
    if (ptr_config_->IsHMC()) {
        std::cerr << "Initialized a memory system with an HMC config file!"
                  << std::endl;
        AbruptExit(__FILE__, __LINE__);
    }

    ctrls_.resize(ptr_config_->channels);
    for (auto i = 0; i < ptr_config_->channels; i++) {
        ctrls_[i] =
#ifdef THERMAL
            new Controller(i, *ptr_config_, *ptr_timing_, *ptr_stats_,
                           ptr_thermCal_, read_callback_, write_callback_);
#else
            new Controller(i, *ptr_config_, *ptr_timing_, *ptr_stats_,
                           read_callback_, write_callback_);
#endif  // THERMAL
    }
}

JedecDRAMSystem::~JedecDRAMSystem() {
    // std::cout << "come to delete JedecDRAMSystem\n";
    for (auto i = 0; i < ptr_config_->channels; i++) {
        delete (ctrls_[i]);
    }
}

bool JedecDRAMSystem::IsReqInsertable(uint64_t hex_addr, bool is_write) {
    CommandType cmd_type = is_write ? CommandType::WRITE : CommandType ::READ;
    Request *temp_req =
        new Request(cmd_type, hex_addr, clk_,
                    0);  // TODO - This is probably not very efficient
    bool status = ctrls_[temp_req->Channel()]->IsReqInsertable(temp_req);
    delete temp_req;
    return status;
}

bool JedecDRAMSystem::InsertReq(uint64_t hex_addr, bool is_write) {
// Record trace - Record address trace for debugging or other purposes
#ifdef GENERATE_TRACE
    address_trace_ << left << setw(18) << clk_ << " " << setw(6) << std::hex
                   << (is_write ? "WRITE " : "READ ") << hex_addr << std::dec
                   << std::endl;
#endif

    CommandType cmd_type = is_write ? CommandType::WRITE : CommandType ::READ;
    id_++;
    Request *req = new Request(cmd_type, hex_addr, clk_, id_);

    bool is_insertable = ctrls_[req->Channel()]->InsertReq(req);
#ifdef NO_BACKPRESSURE
    // Some CPU simulators might not model the backpressure because queues are
    // full. An approximate way of addressing this scenario is to buffer all
    // such requests here in the DRAM simulator and then feed them into the
    // actual memory controller queues as and when space becomes available. Note
    // - This is an approximation and if the size of such buffer queue becomes
    // large during the course of the simulation, then the accuracy sought of
    // devolves into that of a memory address trace based simulation.
    if ((!is_insertable)) {
        buffer_q_.push_back(req);
        is_insertable = true;
        ptr_stats_->numb_buffered_requests++;
    }
#endif
    assert(is_insertable);

    // update interarrival latency
    ptr_stats_->interarrival_latency.AddValue(clk_ - last_req_clk_);
    last_req_clk_ = clk_;

    return is_insertable;
}

void JedecDRAMSystem::ClockTick() {
    for (auto ctrl : ctrls_) ctrl->ClockTick();

#ifdef NO_BACKPRESSURE
    // Insert requests stored in the buffer_q as and when space is available
    if (!buffer_q_.empty()) {
        for (auto req_itr = buffer_q_.begin(); req_itr != buffer_q_.end();
             req_itr++) {
            auto req = *req_itr;
            if (ctrls_[req->Channel()]->InsertReq(req)) {
                buffer_q_.erase(req_itr);
                break;  // either break or set req_itr to the return value of
                        // erase()
            }
        }
    }
#endif

    if (clk_ % ptr_config_->epoch_period == 0 && clk_ != 0) {
        // calculate queue usage each epoch
        // otherwise it would be too inefficient
        int queue_usage_total = 0;
        for (auto ctrl : ctrls_) {
            queue_usage_total += ctrl->QueueUsage();
        }
        ptr_stats_->queue_usage.epoch_value =
            static_cast<double>(queue_usage_total);
        ptr_stats_->PreEpochCompute(clk_);
        PrintIntermediateStats();
        ptr_stats_->UpdateEpoch(clk_);
    }

    clk_++;
    ptr_stats_->dramcycles++;
    return;
}

IdealDRAMSystem::IdealDRAMSystem(
    const std::string &config_file, const std::string &output_dir,
    std::function<void(uint64_t)> read_callback,
    std::function<void(uint64_t)> write_callback)
    : BaseDRAMSystem(config_file, output_dir, read_callback, write_callback),
      latency_(ptr_config_->ideal_memory_latency) {}

IdealDRAMSystem::~IdealDRAMSystem() {}

bool IdealDRAMSystem::InsertReq(uint64_t hex_addr, bool is_write) {
    CommandType cmd_type = is_write ? CommandType::WRITE : CommandType ::READ;
    id_++;
    Request *req = new Request(cmd_type, hex_addr, clk_, id_);
    infinite_buffer_q_.push_back(req);
    return true;
}

void IdealDRAMSystem::ClockTick() {
    clk_++;
    ptr_stats_->dramcycles++;
    for (auto req_itr = infinite_buffer_q_.begin();
         req_itr != infinite_buffer_q_.end(); req_itr++) {
        auto req = *req_itr;
        if (clk_ - req->arrival_time_ >= static_cast<uint64_t>(latency_)) {
            if (req->cmd_.cmd_type == CommandType::READ) {
                ptr_stats_->numb_read_reqs_issued++;
            } else if (req->cmd_.cmd_type == CommandType::WRITE) {
                ptr_stats_->numb_write_reqs_issued++;
            }
            ptr_stats_->access_latency.AddValue(clk_ - req->arrival_time_);
            if (req->cmd_.IsRead())
                read_callback_(req->hex_addr_);
            else if (req->cmd_.IsWrite())
                write_callback_(req->hex_addr_);
            else
                AbruptExit(__FILE__, __LINE__);
            delete (req);
            infinite_buffer_q_.erase(req_itr++);
        } else
            break;  // Requests are always ordered w.r.t to their arrival times,
                    // so need to check beyond.
    }

    if (clk_ % ptr_config_->epoch_period == 0) {
        PrintIntermediateStats();
        ptr_stats_->UpdateEpoch(clk_);
    }
    return;
}

}  // namespace dramcore

