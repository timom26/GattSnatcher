#include "output_handler.h"
#include <rom_print_controller.h>
#include "uart_controller.h"
// // Factory: Get the output handler singleton based on macro switch
// OutputHandler* OutputHandler::getInstance() {
// #ifdef CONFIG_OUTPUT_USE_UART
//     return &UartController::getInstance();
// #else
//     return &RomPrintController::getInstance();
// #endif
// }