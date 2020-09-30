// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#include <math.h>

#include "Math.hh"

/** @brief Convert a floating-point number to a fractional number */
/**
 * See:
 *   https://www.ics.uci.edu/~eppstein/numth/frap.c
 *   https://stackoverflow.com/questions/95727/how-to-convert-floats-to-human-readable-fractions
 *   https://stackoverflow.com/questions/2076290/implementation-limitations-of-float-as-integer-ratio
 *   https://groups.google.com/forum/#!msg/sci.math/8nqj1x7xmWg/umKDlL4N8xgJ
 */
/*
** find rational approximation to given real number
** David Eppstein / UC Irvine / 8 Aug 1993
**
** With corrections from Arno Formella, May 2008
**
** usage: a.out r d
**   r is real number to approx
**   d is the maximum denominator allowed
**
** based on the theory of continued fractions
** if x = a1 + 1/(a2 + 1/(a3 + 1/(a4 + ...)))
** then best approximation is found by truncating this series
** (with some adjustments in the last term).
**
** Note the fraction can be recovered as the first column of the matrix
**  ( a1 1 ) ( a2 1 ) ( a3 1 ) ...
**  ( 1  0 ) ( 1  0 ) ( 1  0 )
** Instead of keeping the sequence of continued fraction terms,
** we just keep the last partial product of these matrices.
*/
void frap(double x, long maxden, long &num, long &den)
{
    long m[2][2];
    double startx;
    long ai;

    startx = x;

    /* initialize matrix */
    m[0][0] = m[1][1] = 1;
    m[0][1] = m[1][0] = 0;

    /* loop finding terms until denom gets too big */
    while (m[1][0] * (ai = (long) x) + m[1][1] <= maxden) {
    	long t;

        t = m[0][0] * ai + m[0][1];
    	m[0][1] = m[0][0];
    	m[0][0] = t;

    	t = m[1][0] * ai + m[1][1];
    	m[1][1] = m[1][0];
    	m[1][0] = t;

        if (x == (double) ai)
            break; // AF: division by zero

    	x = 1/(x - (double) ai);

        if(x > (double) std::numeric_limits<long>::max())
            break;  // AF: representation failure
    }

    /* now remaining x is between 0 and 1/ai */
    /* approx as either 0 or 1/m where m is max that will fit in maxden */
    /* first try zero */
    long n1 = m[0][0];
    long d1 = m[1][0];
    double err1 = fabs(startx - ((double) n1 / (double) d1));

    /* now try other possibility */
    ai = (maxden - m[1][1]) / m[1][0];
    m[0][0] = m[0][0] * ai + m[0][1];
    m[1][0] = m[1][0] * ai + m[1][1];

    long n2 = m[0][0];
    long d2 = m[1][0];
    double err2 = fabs(startx - ((double) n2 / (double) d2));

    if (err2 < err1) {
        num = n2;
        den = d2;
    } else {
        num = n1;
        den = d1;
    }
}
