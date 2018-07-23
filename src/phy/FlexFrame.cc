#include <liquid/liquid.h>

#if LIQUID_VERSION_NUMBER >= 1003001
#define flexframe(x) origflexframe ## x
#else /* LIQUID_VERSION_NUMBER < 1003001 */
#define flexframe(x) flexframe ## x
#endif /* LIQUID_VERSION_NUMBER < 1003001 */

#define FlexFrame FlexFrame

#include "phy/FlexFrame.inc.cc"
