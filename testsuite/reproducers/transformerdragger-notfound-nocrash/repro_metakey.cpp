// Public behavior check shared with CoinTests. See README.md for coverage.
#include <cstdio>
#include <Inventor/SoDB.h>
#include <Inventor/SoInteraction.h>
#include "../../TransformerIgnoredPickTest.h"
static int failures = 0;
static void check(bool passed, const char * message)
{
  if (!passed) { fprintf(stderr, "FAIL: %s\n", message); ++failures; }
}
int main()
{
  SoDB::init();
  SoInteraction::init();
  TransformerIgnoredPickTest::run(true, check);
  SoDB::finish();
  fprintf(stderr, "%s: ignored transformer pick\n", failures ? "FAIL" : "PASS");
  return failures ? 1 : 0;
}
