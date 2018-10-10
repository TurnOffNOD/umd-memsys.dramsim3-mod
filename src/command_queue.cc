#include "command_queue.h"
#include "statistics.h"

namespace dramsim3 {

CommandQueue::CommandQueue(int channel_id, const Config& config,
                           const ChannelState& channel_state, Statistics& stats)
    : clk_(0),
      rank_queues_empty(std::vector<bool>(config.ranks, true)),
      rank_queues_empty_from_time_(std::vector<uint64_t>(config.ranks, 0)),
      config_(config),
      channel_state_(channel_state),
      stats_(stats),
      next_rank_(0),
      next_bankgroup_(0),
      next_bank_(0),
      next_queue_index_(0),
      queue_size_(static_cast<size_t>(config_.cmd_queue_size)),
      channel_id_(channel_id) {
    int num_queues = 0;
    if (config_.queue_structure == "PER_BANK") {
        queue_structure_ = QueueStructure::PER_BANK;
        num_queues = config_.banks * config_.ranks;
    } else if (config_.queue_structure == "PER_RANK") {
        queue_structure_ = QueueStructure::PER_RANK;
        num_queues = config_.ranks;
    } else {
        std::cerr << "Unsupportted queueing structure "
                  << config_.queue_structure << std::endl;
        AbruptExit(__FILE__, __LINE__);
    }

    queues_.reserve(num_queues);
    for (int i = 0; i < num_queues; i++) {
        auto cmd_queue = std::vector<Command>();
        cmd_queue.reserve(config_.cmd_queue_size);
        queues_.push_back(cmd_queue);
    }
}

Command CommandQueue::GetCommandToIssue() {
    for (unsigned i = 0; i < queues_.size(); i++) {
        IterateNext();
        auto cmd = GetFristReadyInQueue(queues_[next_queue_index_]);
        if (!cmd.IsValid()) {
            continue;
        } else {
            if (cmd.cmd_type == CommandType::PRECHARGE) {
                if (!ArbitratePrecharge(cmd)) {
                    return Command();
                }
            }
            return cmd;
        }
    }
    return Command();
}

bool CommandQueue::ArbitratePrecharge(const Command& cmd) {
    auto& queue = GetQueue(cmd.Rank(), cmd.Bankgroup(), cmd.Bank());
    bool pending_row_hits_exist = false;
    auto open_row =
        channel_state_.OpenRow(cmd.Rank(), cmd.Bankgroup(), cmd.Bank());
    for (auto pending_itr = queue.begin(); pending_itr != queue.end();
         pending_itr++) {
        if (pending_itr->Row() == open_row &&
            pending_itr->Bank() == cmd.Bank() &&
            pending_itr->Bankgroup() == cmd.Bankgroup() &&
            pending_itr->Rank() == cmd.Rank()) {
            pending_row_hits_exist = true;
            break;
        }
    }
    bool rowhit_limit_reached =
        channel_state_.RowHitCount(cmd.Rank(), cmd.Bankgroup(), cmd.Bank()) >=
        4;
    if (!pending_row_hits_exist || rowhit_limit_reached) {
        stats_.numb_ondemand_precharges++;
        return true;
    }
    return false;
}

bool CommandQueue::WillAcceptCommand(int rank, int bankgroup, int bank) {
    const auto& queue = GetQueue(rank, bankgroup, bank);
    return queue.size() < queue_size_;
}

bool CommandQueue::AddCommand(Command cmd) {
    auto& queue = GetQueue(cmd.Rank(), cmd.Bankgroup(), cmd.Bank());
    if (queue.size() < queue_size_) {
        queue.push_back(cmd);
        rank_queues_empty[cmd.Rank()] = false;
        return true;
    } else {
        return false;
    }
}

inline void CommandQueue::IterateNext() {
    if (queue_structure_ == QueueStructure::PER_BANK) {
        next_bankgroup_ = (next_bankgroup_ + 1) % config_.bankgroups;
        if (next_bankgroup_ == 0) {
            next_bank_ = (next_bank_ + 1) % config_.banks_per_group;
            if (next_bank_ == 0) {
                next_rank_ = (next_rank_ + 1) % config_.ranks;
            }
        }
    } else if (queue_structure_ == QueueStructure::PER_RANK) {
        next_rank_ = (next_rank_ + 1) % config_.ranks;
    } else {
        std::cerr << "Unknown queue structure" << std::endl;
        AbruptExit(__FILE__, __LINE__);
    }
    next_queue_index_ = GetQueueIndex(next_rank_, next_bankgroup_, next_bank_);
    return;
}

int CommandQueue::GetQueueIndex(int rank, int bankgroup, int bank) {
    if (queue_structure_ == QueueStructure::PER_RANK) {
        return rank;
    } else {
        return rank * config_.banks + bankgroup * config_.banks_per_group +
               bank;
    }
}

std::vector<Command>& CommandQueue::GetQueue(int rank, int bankgroup,
                                             int bank) {
    int index = GetQueueIndex(rank, bankgroup, bank);
    return queues_[index];
}

Command CommandQueue::GetFristReadyInQueue(std::vector<Command>& queue) {
    for (auto cmd_it = queue.begin(); cmd_it != queue.end(); cmd_it++) {
        Command cmd = channel_state_.GetRequiredCommand(*cmd_it);
        // TODO required might be different from cmd_it, e.g. ACT vs READ
        if (channel_state_.IsReady(cmd, clk_)) {
            return cmd;
        }
    }
    return Command();
}

CMDIterator CommandQueue::GetFirstRWInQueue(CMDQueue& queue) {
    for (auto cmd_it = queue.begin(); cmd_it != queue.end(); cmd_it++) {
        int rank, bankgroup, bank;
        rank = cmd_it->Rank();
        bankgroup = cmd_it->Bankgroup();
        bank = cmd_it->Bank();
        if (channel_state_.OpenRow(rank, bankgroup, bank) != -1) {
            if (channel_state_.IsReady(*cmd_it, clk_)) {
                return cmd_it;
            }
        }
    }
    return queue.end();
}

Command CommandQueue::GetFristReadyInBank(int rank, int bankgroup, int bank) {
    // only useful in rank queue
    auto& queue = GetQueue(rank, bankgroup, bank);
    for (auto cmd_it = queue.begin(); cmd_it != queue.end(); cmd_it++) {
        if (cmd_it->Rank() == rank && cmd_it->Bankgroup() == bankgroup &&
            cmd_it->Bank() == bank) {
            Command cmd = channel_state_.GetRequiredCommand(*cmd_it);
            if (channel_state_.IsReady(cmd, clk_)) {
                return cmd;
            }
        }
    }
    return Command();
}

void CommandQueue::IssueRWCommand(const Command& cmd) {
    auto& queue = GetQueue(cmd.Rank(), cmd.Bankgroup(), cmd.Bank());
    for (auto cmd_it = queue.begin(); cmd_it != queue.end(); cmd_it++) {
        if (cmd.id == cmd_it->id) {
            queue.erase(cmd_it);
            return;
        }
    }
}

int CommandQueue::QueueUsage() const {
    int usage = 0;
    for (auto i = queues_.begin(); i != queues_.end(); i++) {
        usage += i->size();
    }
    return usage;
}

}  // namespace dramsim3