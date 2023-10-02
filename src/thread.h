/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2023 The Stockfish developers (see AUTHORS file)

  Stockfish is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  Stockfish is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef THREAD_H_INCLUDED
#define THREAD_H_INCLUDED

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>
#include <tuple>

#include "movepick.h"
#include "position.h"
#include "search.h"
#include "thread_win32_osx.h"

namespace Stockfish {

  namespace UCI {
    class OptionsMap;
  }

  namespace Search {
    struct LimitsType;
    struct Search;
  }

  namespace Tablebases {
    struct Tablebases;
  }

  struct PositionTables;

  struct PSQT;

  struct CommandLine;

  namespace Eval::NNUE {
    struct NNUELoader;
  }



/// Thread class keeps together all the thread-related stuff. We use
/// per-thread pawn and material hash tables so that once we get a
/// pointer to an entry its life time is unlimited and we don't have
/// to care about someone changing the entry under our feet.

struct ThreadPool;
class TimeManagement;
class TranspositionTable;
class Thread {

  std::mutex mutex;
  std::condition_variable cv;
  size_t idx;
  bool exit = false, searching = true; // Set before starting std::thread
  NativeThread stdThread;
  ThreadPool *_threads;
public:
  explicit Thread(ThreadPool*, size_t);
  virtual ~Thread();
  virtual void search();
  void clear();
  void idle_loop();
  void start_searching();
  void wait_for_search_finished();
  size_t id() const { return idx; }
  ThreadPool* threads() { return this->_threads; }

  size_t pvIdx, pvLast;
  std::atomic<uint64_t> nodes, tbHits, bestMoveChanges;
  int selDepth, nmpMinPly;
  Value bestValue, optimism[COLOR_NB];

  Position rootPos;
  StateInfo rootState;
  Search::RootMoves rootMoves;
  Depth rootDepth, completedDepth;
  Value rootDelta;
  CounterMoveHistory counterMoves;
  ButterflyHistory mainHistory;
  CapturePieceToHistory captureHistory;
  ContinuationHistory continuationHistory[2][2];
};


/// MainThread is a derived class specific for main thread

struct MainThread : public Thread {

  using Thread::Thread;

  explicit MainThread(ThreadPool* threads, size_t s): Thread(threads, s), lastInfoTime(now()) {}

  void search() override;
  void check_time();

  TimePoint lastInfoTime;
  //variables below are initialized in thread.cpp  (or we hope so)
  double previousTimeReduction;
  Value bestPreviousScore;
  Value bestPreviousAverageScore;
  Value iterValue[4];
  int callsCnt;
  bool stopOnPonderhit;
  std::atomic_bool ponder;
};

enum SyncCout { IO_LOCK, IO_UNLOCK };

class ThreadIoStreams {
  public:
    ThreadIoStreams(std::istream *in, std::ostream *out)
      : _in(in), _out(out) {}

    std::istream* in() { return _in; }
    std::ostream* out() { return _out; }

    friend std::ostream& operator<<(std::ostream& os, std::tuple<ThreadIoStreams *, SyncCout> t);

  private:
    std::istream *_in;
    std::ostream *_out;

    std::mutex m;
};


/// ThreadPool struct handles all the threads-related stuff like init, starting,
/// parking and, most importantly, launching a thread. All the access to threads
/// is done through this class.

struct ThreadPool {

  ThreadPool(TimeManagement *time, UCI::OptionsMap *options, TranspositionTable *tt, Search::LimitsType *limits, 
      Tablebases::Tablebases *tb, PositionTables *ptb, Search::Search *search, PSQT *psqt, CommandLine *cli,
      Eval::NNUE::NNUELoader *nnue, ThreadIoStreams *io) 
    : _skills_rng(now()),_time(time), _options(options), _tt(tt), _limits(limits), _tb(tb), _ptb(ptb),
    _search(search), _psqt(psqt), _cli(cli), _nnue(nnue), _io(io) {}
  void start_thinking(Position&, StateListPtr&, const Search::LimitsType&, bool = false);
  void clear();
  void set(size_t);

  MainThread* main()        const { return static_cast<MainThread*>(threads.front()); }
  uint64_t nodes_searched() const { return accumulate(&Thread::nodes); }
  uint64_t tb_hits()        const { return accumulate(&Thread::tbHits); }
  Thread* get_best_thread() const;
  void start_searching();
  void wait_for_search_finished() const;

  std::atomic_bool stop, increaseDepth;

  auto cbegin() const noexcept { return threads.cbegin(); }
  auto begin() noexcept { return threads.begin(); }
  auto end() noexcept { return threads.end(); }
  auto cend() const noexcept { return threads.cend(); }
  auto size() const noexcept { return threads.size(); }
  auto empty() const noexcept { return threads.empty(); }

  PRNG* skills_rng() { return &_skills_rng; }

  TimeManagement* time() { return _time; }
  UCI::OptionsMap* options() { return _options; }
  TranspositionTable* tt() { return _tt; }
  Search::LimitsType* limits() { return _limits; }
  Tablebases::Tablebases* tb() { return _tb; }
  PositionTables* ptb() { return _ptb; }
  Search::Search* search() { return _search; }
  PSQT* psqt() { return _psqt; }
  CommandLine *cli() { return _cli; }
  Eval::NNUE::NNUELoader *nnue() { return _nnue; }
  ThreadIoStreams* io() { return _io; }

private:
  StateListPtr setupStates;
  std::vector<Thread*> threads;
  PRNG _skills_rng;
  TimeManagement *_time;
  UCI::OptionsMap *_options;
  TranspositionTable *_tt;
  Search::LimitsType *_limits;
  Tablebases::Tablebases *_tb;
  PositionTables *_ptb;
  Search::Search *_search;
  PSQT *_psqt;
  CommandLine *_cli;
  Eval::NNUE::NNUELoader *_nnue;
  ThreadIoStreams* _io;

  uint64_t accumulate(std::atomic<uint64_t> Thread::* member) const {

    uint64_t sum = 0;
    for (Thread* th : threads)
        sum += (th->*member).load(std::memory_order_relaxed);
    return sum;
  }
};


} // namespace Stockfish

#endif // #ifndef THREAD_H_INCLUDED
