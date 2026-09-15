#pragma once

// Smart water/electricity account used by the room meter.
#define ENERGY_API_URL \
    "CHANGE_ME"
#define ENERGY_ACCOUNT "CHANGE_ME"
#define ENERGY_CUSTOMER_CODE "CHANGE_ME"
#define ENERGY_COMMAND "CHANGE_ME"
#define ENERGY_ROOM_VERIFY "CHANGE_ME"
#define ENERGY_ELECTRICITY_BUSINESS_TYPE 0

// Keep automatic requests infrequent; the page also has a manual refresh.
#define ENERGY_REFRESH_INTERVAL_SECONDS (60 * 60)
