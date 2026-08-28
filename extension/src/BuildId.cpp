#include "BuildId.h"
#include "commit_count.h"   // generated; defines REASIXTY_VERSION_STR

namespace uf8 {

const char* reasixtyBuildId() { return REASIXTY_VERSION_STR; }

}  // namespace uf8
