/*
 * SCPI units and multiplier prefixes, shared by the double and the integer
 * number parsers. Tables and integer code only: integer-only firmware that
 * uses a unit table links nothing from scpi_float.c, and firmware that uses
 * no unit table links nothing from here.
 */
#include "scpi_internal.h"

/* Each table carries the unit parser, so only firmware that names a table
 * links this file: the number parsers reach it through spec->units. */
#define TABLE(name, ...)                                                 \
    static const viper_scpi_unit_t name##_list[] = { __VA_ARGS__, { NULL, 0, 0 } }; \
    const viper_scpi_unit_table_t viper_scpi_units_##name = VIPER_SCPI_UNIT_TABLE(name##_list)

TABLE(volt, { "V", 1.0, 0 });
TABLE(amp, { "A", 1.0, 0 });
TABLE(hertz, { "HZ", 1.0, VIPER_SCPI_UNIT_M_IS_MEGA });
TABLE(ohm, { "OHM", 1.0, VIPER_SCPI_UNIT_M_IS_MEGA });
TABLE(watt, { "W", 1.0, 0 });
TABLE(farad, { "F", 1.0, 0 });
TABLE(henry, { "H", 1.0, 0 });
TABLE(decibel, { "DB", 1.0, 0 });
TABLE(second, { "S", 1.0, 0 }, { "SEC", 1.0, 0 }, { "MIN", 60.0, VIPER_SCPI_UNIT_NO_PREFIX },
      { "HOUR", 3600.0, VIPER_SCPI_UNIT_NO_PREFIX });
TABLE(celsius, { "CEL", 1.0, VIPER_SCPI_UNIT_NO_PREFIX });

/* Multiplier prefixes as powers of 1000. The parsers scale by exact powers
 * of ten, so "10US" is the same value as "10E-6". */
static const struct {
    char name[3];
    int16_t exp3;
} prefixes[] = {
    { "EX", 6 }, { "PE", 5 }, { "MA", 2 }, { "T", 4 }, { "G", 3 }, { "K", 1 },
    { "M", -1 }, { "U", -2 }, { "N", -3 }, { "P", -4 }, { "F", -5 }, { "A", -6 },
};

/* a[0..alen) equals the NUL-terminated b, ignoring case. */
static bool equals_nocase(const char *a, size_t alen, const char *b)
{
    for (size_t i = 0; i < alen; i++) {
        if (b[i] == '\0' || viper_scpi_upper(a[i]) != viper_scpi_upper(b[i])) {
            return false;
        }
    }
    return b[alen] == '\0';
}

/* Resolves a suffix such as "MV" or "KHZ" against @p units: the unit, and the
 * prefix as a power of 1000. NULL if it is not one of them. */
static const viper_scpi_unit_t *lookup_unit(const viper_scpi_unit_t *units, const char *sfx,
                                            size_t n, int *exp3)
{
    *exp3 = 0;
    for (const viper_scpi_unit_t *u = units; u->name != NULL; u++) {
        if (equals_nocase(sfx, n, u->name)) {
            return u;
        }
    }
    for (const viper_scpi_unit_t *u = units; u->name != NULL; u++) {
        if ((u->flags & VIPER_SCPI_UNIT_NO_PREFIX) != 0) {
            continue;
        }
        for (size_t i = 0; i < sizeof prefixes / sizeof prefixes[0]; i++) {
            size_t pl = prefixes[i].name[1] != '\0' ? 2u : 1u; /* "EX" or "K" */

            if (n <= pl || !equals_nocase(sfx, pl, prefixes[i].name) ||
                !equals_nocase(sfx + pl, n - pl, u->name)) {
                continue;
            }
            *exp3 = prefixes[i].exp3;
            if (pl == 1 && prefixes[i].name[0] == 'M' &&
                (u->flags & VIPER_SCPI_UNIT_M_IS_MEGA) != 0) {
                *exp3 = 2;
            }
            return u;
        }
    }
    return NULL;
}

viper_scpi_result_t viper_scpi_unit_split(const viper_scpi_unit_t *units, const char *s, size_t n,
                                          size_t *num, const viper_scpi_unit_t **unit, int *exp3)
{
    size_t i;

    *unit = NULL;
    *exp3 = 0;
    *num = viper_scpi_scan_number(s, n);
    if (*num == 0) {
        return VIPER_SCPI_RES_ERR_DATA_TYPE;
    }
    i = *num;
    while (i < n && viper_scpi_is_space(s[i])) {
        i++;
    }
    if (i == n) {
        return VIPER_SCPI_RES_OK; /* a bare number */
    }
    for (size_t k = i; k < n; k++) {
        char c = viper_scpi_upper(s[k]);
        if (c < 'A' || c > 'Z') {
            return VIPER_SCPI_RES_ERR_INVALID_SUFFIX;
        }
    }
    *unit = lookup_unit(units, s + i, n - i, exp3);
    return (*unit != NULL) ? VIPER_SCPI_RES_OK : VIPER_SCPI_RES_ERR_INVALID_SUFFIX;
}
