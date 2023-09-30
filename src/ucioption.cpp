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

#include <algorithm>
#include <cassert>
#include <ostream>
#include <sstream>

#include "evaluate.h"
#include "misc.h"
#include "search.h"
#include "thread.h"
#include "tt.h"
#include "uci.h"
#include "syzygy/tbprobe.h"

using std::string;

namespace Stockfish {


namespace UCI {

/// 'On change' actions, triggered by an option's value change
static void on_clear_hash(const Option& o) { Search::clear(o.threads()); }
static void on_hash_size(const Option& o) { TT.resize(size_t(o), o.threads()); }
static void on_logger(const Option& o) { start_logger(o); }
static void on_threads(const Option& o) { o.threads()->set(size_t(o)); }
static void on_tb_path(const Option& o) { Tablebases::init(o); }
static void on_eval_file(const Option& o) { Eval::NNUE::init(*o.threads()->options()); }

/// Our case insensitive less() function as required by UCI protocol
bool CaseInsensitiveLess::operator() (const string& s1, const string& s2) const {

  return std::lexicographical_compare(s1.begin(), s1.end(), s2.begin(), s2.end(),
         [](char c1, char c2) { return tolower(c1) < tolower(c2); });
}


/// UCI::init() initializes the UCI options to their hard-coded default values

void init(OptionsMap& o, ThreadPool *threads) {

  constexpr int MaxHashMB = Is64Bit ? 33554432 : 2048;

  o["Debug Log File"]        << Option(threads, "", on_logger);
  o["Threads"]               << Option(threads, 1, 1, 1024, on_threads);
  o["Hash"]                  << Option(threads, 16, 1, MaxHashMB, on_hash_size);
  o["Clear Hash"]            << Option(threads, on_clear_hash);
  o["Ponder"]                << Option(threads, false);
  o["MultiPV"]               << Option(threads, 1, 1, 500);
  o["Skill Level"]           << Option(threads, 20, 0, 20);
  o["Move Overhead"]         << Option(threads, 10, 0, 5000);
  o["Slow Mover"]            << Option(threads, 100, 10, 1000);
  o["nodestime"]             << Option(threads, 0, 0, 10000);
  o["UCI_Chess960"]          << Option(threads, false);
  o["UCI_AnalyseMode"]       << Option(threads, false);
  o["UCI_LimitStrength"]     << Option(threads, false);
  o["UCI_Elo"]               << Option(threads, 1320, 1320, 3190);
  o["UCI_ShowWDL"]           << Option(threads, false);
  o["SyzygyPath"]            << Option(threads, "<empty>", on_tb_path);
  o["SyzygyProbeDepth"]      << Option(threads, 1, 1, 100);
  o["Syzygy50MoveRule"]      << Option(threads, true);
  o["SyzygyProbeLimit"]      << Option(threads, 7, 0, 7);
  o["EvalFile"]              << Option(threads, EvalFileDefaultName, on_eval_file);
}


/// operator<<() is used to print all the options default values in chronological
/// insertion order (the idx field) and in the format defined by the UCI protocol.

std::ostream& operator<<(std::ostream& os, const OptionsMap& om) {

  for (size_t idx = 0; idx < om.size(); ++idx)
      for (const auto& it : om)
          if (it.second.idx == idx)
          {
              const Option& o = it.second;
              os << "\noption name " << it.first << " type " << o.type;

              if (o.type == "string" || o.type == "check" || o.type == "combo")
                  os << " default " << o.defaultValue;

              if (o.type == "spin")
                  os << " default " << int(stof(o.defaultValue))
                     << " min "     << o.min
                     << " max "     << o.max;

              break;
          }

  return os;
}


/// Option class constructors and conversion operators

Option::Option(ThreadPool *threads, const char* v, OnChange f) 
  : type("string"), min(0), max(0), on_change(f), _threads(threads), _tune(nullptr)
{ defaultValue = currentValue = v; }

Option::Option(ThreadPool *threads, bool v, OnChange f) 
  : type("check"), min(0), max(0), on_change(f), _threads(threads), _tune(nullptr)
{ defaultValue = currentValue = (v ? "true" : "false"); }

Option::Option() 
  : type("button"), min(0), max(0), on_change(nullptr), _threads(nullptr), _tune(nullptr)
{}

Option::Option(ThreadPool *threads, OnChange f) 
  : type("button"), min(0), max(0), on_change(f), _threads(threads), _tune(nullptr)
{}

Option::Option(ThreadPool *threads, double v, int minv, int maxv, OnChange f) 
  : type("spin"), min(minv), max(maxv), on_change(f), _threads(threads), _tune(nullptr)
{ defaultValue = currentValue = std::to_string(v); }

Option::Option(ThreadPool *threads, Tune *tune, double v, int minv, int maxv, OnChange f) 
  : type("spin"), min(minv), max(maxv), on_change(f), _threads(threads), _tune(tune)
{ defaultValue = currentValue = std::to_string(v); }


Option::Option(ThreadPool *threads, const char* v, const char* cur, OnChange f) 
  : type("combo"), min(0), max(0), on_change(f), _threads(threads), _tune(nullptr)
{ defaultValue = v; currentValue = cur; }

Option::operator int() const {
  assert(type == "check" || type == "spin");
  return (type == "spin" ? std::stoi(currentValue) : currentValue == "true");
}

Option::operator std::string() const {
  assert(type == "string");
  return currentValue;
}

bool Option::operator==(const char* s) const {
  assert(type == "combo");
  return   !CaseInsensitiveLess()(currentValue, s)
        && !CaseInsensitiveLess()(s, currentValue);
}


/// operator<<() inits options and assigns idx in the correct printing order

void Option::operator<<(const Option& o) {

  static size_t insert_order = 0;

  *this = o;
  idx = insert_order++;
}


/// operator=() updates currentValue and triggers on_change() action. It's up to
/// the GUI to check for option's limits, but we could receive the new value
/// from the user by console window, so let's check the bounds anyway.

Option& Option::operator=(const string& v) {

  assert(!type.empty());

  if (   (type != "button" && type != "string" && v.empty())
      || (type == "check" && v != "true" && v != "false")
      || (type == "spin" && (stof(v) < min || stof(v) > max)))
      return *this;

  if (type == "combo")
  {
      OptionsMap comboMap; // To have case insensitive compare
      string token;
      std::istringstream ss(defaultValue);
      while (ss >> token)
          comboMap[token] << Option();
      if (!comboMap.count(v) || v == "var")
          return *this;
  }

  if (type != "button")
      currentValue = v;

  if (on_change)
      on_change(*this);

  return *this;
}

} // namespace UCI

} // namespace Stockfish
