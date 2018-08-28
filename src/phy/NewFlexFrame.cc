#include <complex>

#include <liquid/liquid.h>

#if LIQUID_VERSION_NUMBER >= 1003001
#define NEWFLEXFRAME
#endif /* LIQUID_VERSION_NUMBER >= 1003000 */

#define flexframe(x) flexframe ## x

#define FlexFrame NewFlexFrame

#include "phy/FlexFrame.inc.cc"
