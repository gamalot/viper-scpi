#include "viper_scpi/viper_scpi.h"

int main(void)
{
    static viper_scpi_context_t ctx;
    static const viper_scpi_config_t cfg = { .root = NULL };
    return viper_scpi_init(&ctx, &cfg) == VIPER_SCPI_RES_OK ? 0 : 1;
}
