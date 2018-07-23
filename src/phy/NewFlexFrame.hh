#ifndef NEWFLEXFRAME_H_
#define NEWFLEXFRAME_H_

#define flexframe(x) flexframe ## x

#define FlexFrame NewFlexFrame

#include "phy/FlexFrame.inc.hh"

#undef flexframe
#undef FlexFrame

#endif /* NEWFLEXFRAME_H_ */
