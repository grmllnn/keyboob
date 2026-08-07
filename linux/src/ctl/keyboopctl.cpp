#include "engine.hpp"
#include "keymap.hpp"
#include "layout_data.hpp"
#include "layout_detector.hpp"
#include "utf8.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

static void usage() {
  std::cerr
      << "keyboopctl — offline helpers for Keyboop Linux\n"
      << "  convert <word>     convert word to the other layout\n"
      << "  decide <word>      print detector decision (keep|to-ru|to-en)\n"
      << "  version\n";
}

int main(int argc, char **argv) {
  if (argc < 2) {
    usage();
    return 1;
  }
  std::string cmd = argv[1];
  if (cmd == "version") {
    std::cout << "keyboopctl 0.1.0\n";
    return 0;
  }

  const char *dir = std::getenv("KEYBOOP_DATA_DIR");
  std::string path = dir ? dir : KEYBOOP_DEFAULT_DATA_DIR;
  keyboop::LayoutData::shared().load(path);

  if (cmd == "convert" && argc >= 3) {
    std::string w = argv[2];
    bool to_cyr = keyboop::has_latin_letter(w) && !keyboop::has_cyrillic(w);
    if (keyboop::has_cyrillic(w) && !keyboop::has_latin_letter(w))
      to_cyr = false;
    std::cout << keyboop::Keymap::convert(w, to_cyr) << "\n";
    return 0;
  }
  if (cmd == "decide" && argc >= 3) {
    keyboop::ExceptionStore exc;
    auto d = keyboop::LayoutDetector::decide(argv[2], exc);
    if (d.is_keep())
      std::cout << "keep\n";
    else
      std::cout << (d.to_cyrillic ? "to-ru\n" : "to-en\n");
    return 0;
  }
  usage();
  return 1;
}
