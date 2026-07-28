#pragma once

#include "AgentConfig.h"
#include "types.h"

#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <unordered_map>

class TaskManager {
public:
    explicit TaskManager(const codex_lan_agent::AgentConfig & config);
    ~TaskManager();

    std::string EnqueueCliProfile(
        const std::string & profile,
        const std::string & args,
        int timeout_sec_override = -1,
        int stall_timeout_sec_override = -1);
    std::string EnqueueCxParserRuntime(const std::string & flow_id, const std::string & args);
    std::string EnqueueCase(const std::string & case_path);
    std::string EnqueueRagFlow(const std::string & query, const std::string & mode);
    std::string EnqueueLocalChat(const std::string & scope, const std::string & question, const std::string & mode);
    CommandResult GetTaskResult(const std::string & task_id) const;
    CommandResult GetLatestTaskResult() const;
    CommandResult ListTaskResults(int max_entries) const;
    int QueueDepth() const;

private:
    std::string EnqueueTask(
        TaskKind kind,
        const std::string & arg1,
        const std::string & arg2,
        int timeout_sec_override = -1,
        int stall_timeout_sec_override = -1);
    void PruneCompletedTasksLocked();
    void WorkerLoop();
    static std::string StatusTimeStamp();
    static std::string TaskKindName(TaskKind kind);

    const codex_lan_agent::AgentConfig & config_;
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::unordered_map<std::string, TaskRecord> tasks_;
    std::deque<std::string> pending_ids_;
    std::deque<std::string> completed_ids_;
    std::thread worker_;
    bool stop_ = false;
    unsigned long long next_id_ = 1;
    std::size_t max_completed_history_ = 100;
};
