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
#include <iostream>
#include <sstream>

#include "types.h"
#include "misc.h"
#include "uci.h"
#include "thread.h"

using std::string;

namespace Stockfish {

const UCI::Option* LastOption = nullptr;
//elosev: This static doesn't seem to be used in the code and is always empty. Turn it into const
//and replace with an assert (which is BAD).
static const std::map<std::string, int> TuneResults;

string Tune::next(string& names, bool pop) {

  string name;

  do {
      string token = names.substr(0, names.find(','));

      if (pop)
          names.erase(0, token.size() + 1);

      std::stringstream ws(token);
      name += (ws >> token, token); // Remove trailing whitespace

  } while (  std::count(name.begin(), name.end(), '(')
           - std::count(name.begin(), name.end(), ')'));

  return name;
}

static void on_tune(const UCI::Option& o) {

  if (!o.tune()->update_on_last || LastOption == &o)
      o.tune()->read_options();
}

static void make_option(Tune *tune, const string& n, int v, const SetRange& r) {

  // Do not generate option when there is nothing to tune (ie. min = max)
  if (r(v).first == r(v).second)
      return;

  //elosev: As TuneResults seems to be not used commenting out this code, replacing with an assert
  //for future. If this variable ever gets used we need to extract it into local context
  //if (TuneResults.count(n))
  //    v = TuneResults[n];
  assert(!TuneResults.count(n));

  UCI::OptionsMap *options = tune->threads()->options();
  (*options)[n] << UCI::Option(tune->threads(), tune, v, r(v).first, r(v).second, on_tune);
  LastOption = &(*options)[n];

  // Print formatted parameters, ready to be copy-pasted in Fishtest
  std::cout << n << ","
            << v << ","
            << r(v).first << "," << r(v).second << ","
            << (r(v).second - r(v).first) / 20.0 << ","
            << "0.0020"
            << std::endl;
}

template<> void Tune::Entry<int>::init_option() { make_option(tune, name, value, range); }

template<> void Tune::Entry<int>::read_option() {
  if (tune->threads()->options()->count(name))
      value = int((*tune->threads()->options())[name]);
}

template<> void Tune::Entry<Value>::init_option() { make_option(tune, name, value, range); }

template<> void Tune::Entry<Value>::read_option() {
  if (tune->threads()->options()->count(name))
      value = Value(int((*tune->threads()->options())[name]));
}

template<> void Tune::Entry<Score>::init_option() {
  make_option(tune, "m" + name, mg_value(value), range);
  make_option(tune, "e" + name, eg_value(value), range);
}

template<> void Tune::Entry<Score>::read_option() {
  if (tune->threads()->options()->count("m" + name))
      value = make_score(int((*tune->threads()->options())["m" + name]), eg_value(value));

  if (tune->threads()->options()->count("e" + name))
      value = make_score(mg_value(value), int((*tune->threads()->options())["e" + name]));
}

// Instead of a variable here we have a PostUpdate function: just call it
template<> void Tune::Entry<Tune::PostUpdate>::init_option() {}
template<> void Tune::Entry<Tune::PostUpdate>::read_option() { value(); }

} // namespace Stockfish


// Init options with tuning session results instead of default values. Useful to
// get correct bench signature after a tuning session or to test tuned values.
// Just copy fishtest tuning results in a result.txt file and extract the
// values with:
//
// cat results.txt | sed 's/^param: \([^,]*\), best: \([^,]*\).*/  TuneResults["\1"] = int(round(\2));/'
//
// Then paste the output below, as the function body

#include <cmath>

namespace Stockfish {

void Tune::read_results() {

  /* ...insert your values here... */
}

} // namespace Stockfish
