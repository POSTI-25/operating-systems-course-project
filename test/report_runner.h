#ifndef SAGE_REPORT_RUNNER_H
#define SAGE_REPORT_RUNNER_H
#include "phase5_report.h"

bool report_runner_write(FILE *output);
bool report_runner_compare(FILE *output, const char *scenario,
    const Phase5Metrics *full, const char *comparator_name,
    const Phase5Metrics *comparator);
#endif
