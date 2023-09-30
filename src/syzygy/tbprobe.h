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

#ifndef TBPROBE_H
#define TBPROBE_H

#include <ostream>
#include <utility>

#include "../search.h"

namespace Stockfish {
  namespace UCI {
    class OptionsMap;
  }
}

namespace Stockfish::Tablebases {

enum WDLScore {
    WDLLoss        = -2, // Loss
    WDLBlessedLoss = -1, // Loss, but draw under 50-move rule
    WDLDraw        =  0, // Draw
    WDLCursedWin   =  1, // Win, but draw under 50-move rule
    WDLWin         =  2, // Win
};

// Possible states after a probing operation
enum ProbeState {
    FAIL              =  0, // Probe failed (missing file table)
    OK                =  1, // Probe successful
    CHANGE_STM        = -1, // DTZ should check the other side
    ZEROING_BEST_MOVE =  2  // Best move zeroes DTZ (capture or pawn move)
};

class TBTables;
struct Tablebases {
  Tablebases();
  ~Tablebases();

  void init(ThreadPool *threads, const std::string& paths);

  int MaxCardinality;

  TBTables* tb_tables() {
    return _tb_tables.get();
  }

  const std::string& paths() const {
    return _paths;
  }

  WDLScore probe_wdl(Position& pos, ProbeState* result);
  int probe_dtz(Position& pos, ProbeState* result);
  bool root_probe(UCI::OptionsMap* options,Position& pos, Search::RootMoves& rootMoves);
  bool root_probe_wdl(UCI::OptionsMap* options,Position& pos, Search::RootMoves& rootMoves);
  void rank_root_moves(UCI::OptionsMap* options, Position& pos, Search::RootMoves& rootMoves);

  bool pawns_comp(Square i, Square j) { return MapPawns[i] < MapPawns[j]; }

  int MapPawns[SQUARE_NB];
  int MapB1H1H7[SQUARE_NB];
  int MapA1D1D4[SQUARE_NB];
  int MapKK[10][SQUARE_NB]; // [MapA1D1D4][SQUARE_NB]

  int Binomial[6][SQUARE_NB];    // [k][n] k elements from a set of n elements
  int LeadPawnIdx[6][SQUARE_NB]; // [leadPawnsCnt][SQUARE_NB]
  int LeadPawnsSize[6][4];       // [leadPawnsCnt][FILE_A..FILE_D]

private:
  std::unique_ptr<TBTables> _tb_tables; 
  std::string _paths;
};


inline std::ostream& operator<<(std::ostream& os, const WDLScore v) {

    os << (v == WDLLoss        ? "Loss" :
           v == WDLBlessedLoss ? "Blessed loss" :
           v == WDLDraw        ? "Draw" :
           v == WDLCursedWin   ? "Cursed win" :
           v == WDLWin         ? "Win" : "None");

    return os;
}

inline std::ostream& operator<<(std::ostream& os, const ProbeState v) {

    os << (v == FAIL              ? "Failed" :
           v == OK                ? "Success" :
           v == CHANGE_STM        ? "Probed opponent side" :
           v == ZEROING_BEST_MOVE ? "Best move zeroes DTZ" : "None");

    return os;
}

} // namespace Stockfish::Tablebases

#endif
