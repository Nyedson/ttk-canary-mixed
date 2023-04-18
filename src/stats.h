#ifndef TFS_STATS_H
#define TFS_STATS_H

#include "utils/thread_holder_base.h"

#include <forward_list>
#include <atomic>

class Task;

#ifdef STATS_ENABLED
#define createTask(function) createNewTask(function, #function, __FUNCTION__)
#define createSchedulerTask(delay, function) createNewSchedulerTask(delay, function, #function, __FUNCTION__)
#define addGameTask(function, ...) addNewGameTask(function, #function, __FUNCTION__, __VA_ARGS__)
#define addGameTaskTimed(delay, function, ...) addNewGameTaskTimed(delay, function, #function, __FUNCTION__, __VA_ARGS__)
#else
#define createTask(function) createNewTask(function, "", "")
#define createSchedulerTask(delay, function) createNewSchedulerTask(delay, function, "", "")
#define addGameTask(function, ...) addNewGameTask(function, "", "", __VA_ARGS__)
#define addGameTaskTimed(delay, function, ...) addNewGameTaskTimed(delay, function, "", "", __VA_ARGS__)
#endif

struct Stat {
    Stat(uint64_t _executionTime, const std::string& _description, const std::string& _extraDescription) :
            executionTime(_executionTime), description(_description), extraDescription(_extraDescription) {};
    uint64_t executionTime = 0;
    std::string description;
    std::string extraDescription;
};

struct statsData {
    statsData(uint32_t _calls, uint64_t _executionTime, const std::string& _extraInfo) :
            calls(_calls), executionTime(_executionTime), extraInfo(_extraInfo) {}
    uint32_t calls = 0;
    uint64_t executionTime = 0;
    std::string extraInfo;
};

using statsMap = std::map<std::string, statsData>;

class Stats : public ThreadHolder<Stats> {
	public:
		void threadMain();
		void shutdown() {
			setState(THREAD_STATE_TERMINATED);
		}

		void addDispatcherTask(int index, Task* task);
		void addLuaStats(Stat* stats);
		void addSqlStats(Stat* stats);
		std::atomic<uint64_t>& dispatcherWaitTime(int index) {
			return dispatchers[index].waitTime;
		}

		static uint32_t SLOW_EXECUTION_TIME;
		static uint32_t VERY_SLOW_EXECUTION_TIME;
		static int64_t DUMP_INTERVAL;

		std::atomic<uint32_t> playersOnline;

	private:
		void parseDispatchersQueue(std::vector<std::forward_list < Task * >> queues);
		void parseLuaQueue(std::forward_list <Stat*>& queue);
		void parseSqlQueue(std::forward_list <Stat*>& queue);
		void writeSlowInfo(const std::string& file, uint64_t executionTime, const std::string& description, const std::string& extraDescription);
		void writeStats(const std::string& file, const statsMap& stats, const std::string& extraInfo = "");

		std::mutex statsLock;
		struct {
			std::forward_list <Task*> queue;
			statsMap stats;
			std::atomic<uint64_t> waitTime;
			int64_t lastDump;
		} dispatchers[3];
		struct {
			std::forward_list <Stat*> queue;
			statsMap stats;
			int64_t lastDump;
		} lua, sql;
};

extern Stats g_stats;

#endif