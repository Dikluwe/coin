#include <Inventor/SoDB.h>
#include <Inventor/lists/SbList.h>
#include <Inventor/scxml/ScXMLEvent.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static volatile size_t sink = 0;

int
main(int argc, char ** argv)
{
  if (argc != 3) return 2;
  const int count = std::atoi(argv[1]);
  const int iterations = std::atoi(argv[2]);
  if (count < 1 || count > 8 || iterations < 1) return 3;

  const char * names[] = { "a", "b", "c", "d", "e", "f", "g", "h" };
  SoDB::init();
  ScXMLEvent event;
  for (int i = 0; i < count; ++i) event.setAssociation(names[i], "value");

  typedef std::chrono::steady_clock clock;
  const clock::time_point getstart = clock::now();
  for (int i = 0; i < iterations; ++i) {
    const char * value = event.getAssociation(names[i % count]);
    sink += value ? std::strlen(value) : 0;
  }
  const clock::time_point getend = clock::now();

  const clock::time_point setstart = clock::now();
  for (int i = 0; i < iterations; ++i) {
    event.setAssociation(names[i % count], (i & 1) ? "first" : "second");
  }
  const clock::time_point setend = clock::now();

  const clock::time_point keysstart = clock::now();
  for (int i = 0; i < iterations; ++i) {
    SbList<const char *> keys;
    sink += event.getAssociationKeys(keys);
  }
  const clock::time_point keysend = clock::now();

  const double getns = std::chrono::duration<double, std::nano>(getend - getstart).count() / iterations;
  const double setns = std::chrono::duration<double, std::nano>(setend - setstart).count() / iterations;
  const double keysns = std::chrono::duration<double, std::nano>(keysend - keysstart).count() / iterations;
  std::printf("keys=%d get=%.2f set=%.2f keys=%.2f ns/op sink=%zu\n",
              count, getns, setns, keysns, sink);
  return 0;
}
