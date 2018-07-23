#ifndef FLEXFRAME_H_
#define FLEXFRAME_H_

#if LIQUID_VERSION_NUMBER >= 1003001
#define flexframe(x) origflexframe ## x
#else /* LIQUID_VERSION_NUMBER < 1003001 */
#define flexframe(x) flexframe ## x
#endif /* LIQUID_VERSION_NUMBER < 1003001 */

#define FlexFrame FlexFrame

#include "phy/FlexFrame.inc.hh"

#undef flexframe
#undef FlexFrame

#endif /* FLEXFRAME_H_ */
