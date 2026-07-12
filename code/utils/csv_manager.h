
#ifndef CSV_MANAGER_H
#define CSV_MANAGER_H


#include "../shared_state.h"
#include "../utils/errors.h"
#include "../encoding/crypto.h"

int saveBlockchainCsv(const SharedState *ss, const char *filename);

int loadCsv(const char *filename, SharedState *ss);

#endif
