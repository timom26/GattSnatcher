 #include "struct_and_definitions.h"


bool LeAdvertisingReport::isAdvertisingReportConnectable() const
{
    for (uint8_t i = 0; i < this->num_reports; i++) {
        uint8_t adv_type = this->reports[i].adv_event_type;
        // ADV_IND (0x00) and ADV_DIRECT_IND (0x01) are connectable types
        if (adv_type == 0x00 || adv_type == 0x01) {
            return true;
        }
    }
    return false;
}
