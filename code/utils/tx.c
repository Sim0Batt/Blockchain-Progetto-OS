#include <regex.h>
#include <stddef.h>

#include "tx.h"
#include "../utils/errors.h"

/* Formato richiesto: "Nome pays Nome Importo coins", con importo intero
 * positivo e senza zeri iniziali (es. "10", non "010"). */
static const char *TX_PATTERN = "^[A-Za-z0-9]+ pays [A-Za-z0-9]+ [1-9][0-9]* coins$";

int txIsValid(const char *s) {
    if (s == NULL) {
        return INVALID_TRANSACTION;
    }

    regex_t regex;
    if (regcomp(&regex, TX_PATTERN, REG_EXTENDED) != 0) {
        // Il pattern e' una costante: se regcomp fallisce non e' colpa dell'input
        return PARSE_ERROR;
    }

    int matched = regexec(&regex, s, 0, NULL, 0);
    regfree(&regex);

    return (matched == 0) ? SUCCESS : INVALID_TRANSACTION;
}
