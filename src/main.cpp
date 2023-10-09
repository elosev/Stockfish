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

#include <iostream>
#include <string>
#include "ffilebuf2"
#include <unistd.h>
#include <pthread.h>
#include <stdio.h>
#include <fcntl.h>

#include "bitboard.h"
#include "position.h"
#include "psqt.h"
#include "search.h"
#include "syzygy/tbprobe.h"
#include "thread.h"
#include "tt.h"
#include "uci.h"
#include <mutex>

//new includes
#include "timeman.h"
#include "thread.h"
#include "search.h"

using namespace Stockfish;
typedef basic_ofstream2 ofstream2;
typedef basic_ifstream2 ifstream2;

extern "C" int stockfish_thread_wrapper(int pipe_in, int pipe_out, int argc, char* argv[]) {
  //as the call is reusing global cin and cout making sure that we have only one instance of this function
  //running at a time. Fixing this requires geting rid of global cin/cout and pasing cin/cout to all 
  //stockfish functions 
  static std::mutex mtx;
  //true, if Bitboards have been initialized
  static bool bitboards_initialized= false;


  //fstream2 is a modified version of fstream which supports reads from pipe
  //it is done by hardcoding behaviour of underflow to consume the pipe one byte at a time
  //as otherwise fread blocks if it is requesting more bytes than the pipe has
  ifstream2 fin;
  ofstream2 fout;

  fin.open(pipe_in);
  fout.open(pipe_out);


  ///----
  ThreadIoStreams io(&fin, &fout);
  Eval::NNUE::NNUELoader nnue;
  CommandLine cli;
  PSQT psqt;
  Search::Search search;
  PositionTables ptb;
  Search::LimitsType Limits;
  TranspositionTable TT; // Our global TranspositionTable
  UCI::OptionsMap Options; // Global object
  TimeManagement Time;// Our global time management object
  Tablebases::Tablebases tb;
  ThreadPool threads(&Time, &Options, &TT, &Limits, &tb, &ptb, &search, &psqt, &cli, &nnue, &io); // Global object
  Tune tune(&threads);

  //call this once, it initalizes constant structure
  {
    std::unique_lock<std::mutex> lock(mtx);
    if (!bitboards_initialized) {
      Bitboards::init();
    }
    bitboards_initialized = true;
  }


  *io.out() << engine_info() << std::endl;
  cli.init(argc, argv);
  UCI::init(Options, &threads);

  //elosev: calling this after options are initialized (see tune.h)
  tune.init();
  psqt.init();
  ptb.init();
  threads.set(size_t(Options["Threads"]));
  search.clear(&threads); // After threads are up
  nnue.init(&threads);

  UCI::loop(argc, argv, &threads);

  threads.set(0);
  return 0;
}

struct ThreadParams {
  int pipe_in;
  int pipe_out;
  int argc;
  char **argv;
};

extern "C" void *thread_main(void *p) {
  ThreadParams *params = (ThreadParams *)p; 
  printf("Hello from thread! p_in:%d p_out:%d\n", params->pipe_in, params->pipe_out);

  stockfish_thread_wrapper(params->pipe_in, params->pipe_out, params->argc, params->argv);

  printf("Goodbye from thread!\n");

  return 0;
}

int pipe_wrapper(int argc, char* argv[]) {
  int fd_in[2] = {0, 0}; 
  int fd_out[2] = {0, 0}; 

  if (pipe(fd_in)) {
    printf("Cannot create fd_in\n");
    return 1;
  }
  if (pipe(fd_out)) {
    printf("Cannot create fd_in\n");
    return 1;
  }

  printf("fd_in: %d,%d; fd_out:%d,%d\n",
      fd_in[0], fd_in[1],
      fd_out[0], fd_out[1]);

  ThreadParams params = {
    .pipe_in = fd_in[0],
    .pipe_out = fd_out[1],
    .argc = argc,
    .argv = argv
  };

  ofstream2 file_in;
  ifstream2 file_out;

  file_in.open(fd_in[1]);
  file_out.open(fd_out[0]);


  if (!file_in.is_open())  {
    printf("file_in is closed\n");
    return 1;
  }
  if (!file_out.is_open())  {
    printf("file_out is closed\n");
    return 1;
  }

  pthread_t thread_id = 0;
  if (pthread_create(&thread_id, NULL, thread_main, &params)) {
    printf("Cannot create thread\n");
    return 1;
  }

  printf("Thread id=0x%lx is ready. Reading...\n", thread_id);


  file_in << "uci" << std::endl;
  file_in << "setoption name Threads value 5" << std::endl;
  file_in << "go movetime 3500" << std::endl;
  sleep(5);
  file_in << "quit" << std::endl;


  std::string temp;
  while (std::getline(file_out, temp)) {
    printf(">>> %s\n", temp.c_str());
  }
 
  if (pthread_join(thread_id, NULL)) {
    printf("Cannot join thread!\n");
  }

  file_out.close();
  file_in.close();

  return 0;
}


void* pipe_wrapper_wrapper(void *) {

  char *argv[] = { "/foo/bar" };

  pipe_wrapper(1, argv);

  return NULL;
}

int main(int argc, char* argv[]) {
  printf(">>>>>>1\n");

  const int thread_num = 10;

  pthread_t threads[thread_num];

  for (int i = 0; i < thread_num; ++i) {
    if (pthread_create(&threads[i], NULL, pipe_wrapper_wrapper, nullptr)) {
      printf("Cannot create thread\n");
      return 1;
    }
  }

  for (int i = 0; i < thread_num; ++i) {
    if (pthread_join(threads[i], NULL)) {
      printf("Cannot join thread!\n");
    }
  }

  printf(">>>>>>2\n");

  return pipe_wrapper(argc, argv);
}

/*int main(int argc, char* argv[]) {
  //elosev: options is created before ThreadPool, but take a reference to i later in initialization.
  //It introduces circular reference, and potentially creates risks during destruction where options
  //may have reference to already deleted ThreadPool. It seems to be ok, as Options are not used
  //after we exit from UCI::loop
  ThreadIoStreams io(&std::cin, &std::cout);
  Eval::NNUE::NNUELoader nnue;
  CommandLine cli;
  PSQT psqt;
  Search::Search search;
  PositionTables ptb;
  Search::LimitsType Limits;
  TranspositionTable TT; // Our global TranspositionTable
  UCI::OptionsMap Options; // Global object
  TimeManagement Time;// Our global time management object
  Tablebases::Tablebases tb;
  ThreadPool threads(&Time, &Options, &TT, &Limits, &tb, &ptb, &search, &psqt, &cli, &nnue, &io); // Global object
  Tune tune(&threads);


  *io.out() << engine_info() << std::endl;

  cli.init(argc, argv);
  UCI::init(Options, &threads);
  //elosev: calling this after options are initialized (see tune.h)
  
  //call this once, it initalizes constant structure
  Bitboards::init();


  tune.init();
  psqt.init();
  ptb.init();
  threads.set(size_t(Options["Threads"]));
  search.clear(&threads); // After threads are up
  nnue.init(&threads);

  UCI::loop(argc, argv, &threads);

  threads.set(0);
  return 0;
}*/
