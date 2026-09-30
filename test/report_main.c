#include "report_runner.h"
int main(void)
{
    return report_runner_write(stdout) ? 0 : 1;
}
