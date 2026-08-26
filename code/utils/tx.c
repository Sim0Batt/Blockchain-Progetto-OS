#include <regex.h>
#include <stddef.h>

#include "tx.h"
#include "../utils/errors.h"

/* Pattern fisso richiesto dal PDF: "Nome pays Nome Importo coins", con
 * importo intero positivo senza zeri iniziali (es. "10", non "010"). */
static const char *TX_PATTERN = "^[A-Za-z0-9]+ pays [A-Za-z0-9]+ [1-9][0-9]* coins$";

// Valida il formato di una transazione tramite regex POSIX.
int txIsValid(const char *s) {
    if (s == NULL) {
        return INVALID_TRANSACTION;
    }

    regex_t regex;
    if (regcomp(&regex, TX_PATTERN, REG_EXTENDED) != 0) {
        // Il pattern e' fisso: se regcomp fallisce e' un errore di programmazione, non di input
        return PARSE_ERROR;
    }

    int matched = regexec(&regex, s, 0, NULL, 0);
    regfree(&regex);

    return (matched == 0) ? SUCCESS : INVALID_TRANSACTION;
}
