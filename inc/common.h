#ifndef COMMON_H
#define COMMON_H

#include "log.h"

#define START   do { Info("Start > "); int STATUS = EXIT_SUCCESS;

#define FAIL(x) STATUS = EXIT_FAILURE; break; // Fail and break
#define CHECKNULL(x) if (!x)     { Warn("Null pointer: %s", #x); }
#define CHECKZERO(x) if (x == 0) { Warn("Zero value: %s"  , #x); }

#define END     Info("< End:"); } while(0);
#define RETURN  END; return STATUS;


#endif//COMMON_H
