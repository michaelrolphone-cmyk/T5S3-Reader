#include "FontCatalogValidation.h"

#include <cassert>
#include <string>
#include <vector>

struct Family {
  std::string name;
  std::vector<std::string> files;
};

int main() {
  std::vector<Family> published{{"Known", {"known.cpfont"}}};

  // A later invalid entry must not publish an earlier valid prefix.
  std::vector<Family> malformed{{"Fresh", {"fresh.cpfont"}}, {"Broken", {}}};
  assert(!FontCatalogValidation::publish(published, malformed, true));
  assert(published.size() == 1 && published[0].name == "Known");

  // Missing or wrong-type top-level families are rejected; a retry can commit.
  std::vector<Family> retry{{"Fresh", {"fresh.cpfont"}}};
  assert(!FontCatalogValidation::publish(published, retry, false));
  assert(published.size() == 1 && published[0].name == "Known");
  std::vector<Family> validRetry{{"Fresh", {"fresh.cpfont"}}};
  assert(FontCatalogValidation::publish(published, validRetry, true));
  assert(published.size() == 1 && published[0].name == "Fresh");

  // Empty packages cannot become phantom installed rows, and names must be
  // unique under the case-insensitive SD filesystem identity.
  std::vector<Family> emptyPackage{{"Empty", {}}};
  assert(!FontCatalogValidation::publish(published, emptyPackage, true));
  std::vector<Family> duplicateNames{{"NotoSans", {"a.cpfont"}},
                                     {"notosans", {"b.cpfont"}}};
  assert(!FontCatalogValidation::publish(published, duplicateNames, true));
  assert(published.size() == 1 && published[0].name == "Fresh");

  // An explicitly empty array remains a valid empty catalog.
  std::vector<Family> emptyCatalog;
  assert(FontCatalogValidation::publish(published, emptyCatalog, true));
  assert(published.empty());
}
