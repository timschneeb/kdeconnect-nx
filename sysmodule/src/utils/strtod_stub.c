/**
 * strtod is currently only used by yyjson.
 * Though, KDE Connect's protocol doesn't make use of floats (except in the drawing pad plugin, which we don't support).
 * Because of this we just replace strtod with a simple replacement to avoid pulling in newlib's __gethex / locale machinery
 * and bloating the binary size since that costs us RAM!
 *
 * Handles the JSON number syntax that yyjson produces: optional sign, decimal integer, optional fractional part,
 * optional decimal exponent. Hex floats, infinity, and NaN are not needed and not implemented.
 */

double __wrap_strtod(const char *s, char **endptr) {
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;

    double sign = 1.0;
    if      (*s == '-') { sign = -1.0; s++; }
    else if (*s == '+') {              s++; }

    double val = 0.0;
    const char *p = s;

    while (*p >= '0' && *p <= '9')
        val = val * 10.0 + (double)(*p++ - '0');

    if (*p == '.') {
        p++;
        double place = 0.1;
        while (*p >= '0' && *p <= '9') {
            val += (double)(*p++ - '0') * place;
            place *= 0.1;
        }
    }

    if (*p == 'e' || *p == 'E') {
        p++;
        int esign = 1;
        if      (*p == '-') { esign = -1; p++; }
        else if (*p == '+') {             p++; }
        int exp = 0;
        while (*p >= '0' && *p <= '9')
            exp = exp * 10 + (*p++ - '0');
        if (exp > 308) exp = 308;
        double pow10 = 1.0;
        for (int i = 0; i < exp; i++) pow10 *= 10.0;
        if (esign > 0) val *= pow10; else val /= pow10;
    }

    if (endptr) *endptr = (char *)p;
    return sign * val;
}
