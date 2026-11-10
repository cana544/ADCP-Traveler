#include <cassert>
#include <cstring>
#include <iostream>
#include "diagnostic_commands.h"
int main() {
  DiagnosticCommands parser;
  for (char c : "LOG ON\r") assert(parser.feed(c) == DiagnosticCommands::NONE);
  assert(parser.feed('\n') == DiagnosticCommands::LOG_ON);
  for (int i = 0; i < 100; ++i) parser.feed('A');
  for (char c : "STOP") assert(parser.feed(c) == DiagnosticCommands::NONE);
  assert(parser.feed('\n') == DiagnosticCommands::INVALID);
  for (char c : "STOP") if (c) assert(parser.feed(c) == DiagnosticCommands::NONE);
  assert(parser.feed('\n') == DiagnosticCommands::STOP);
  for (char c : "MOVE 100 1") if (c) parser.feed(c);
  assert(parser.feed('\n') == DiagnosticCommands::INVALID);
  for (char c : "ADAPT OFF") if (c) parser.feed(c);
  assert(parser.feed('\n') == DiagnosticCommands::ADAPT_OFF);
  for (const char* command : {"ADAPT START ON", "ADAPT START OFF", "ADAPT LOAD ON", "ADAPT LOAD OFF"}) {
    for (const char* c = command; *c; ++c) parser.feed(*c);
    assert(parser.feed('\n') != DiagnosticCommands::INVALID);
  }
  std::cout << "Bounded USB command parsing passed\n";
}
