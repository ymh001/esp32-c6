#pragma once

// Smart water/electricity account used by the room meter.
#define ENERGY_API_URL \
    "https://h5.wanmeiqiye.com/smartWaterAndElectricityService_V2_1/SWAEServlet"
#define ENERGY_ACCOUNT "000517"
#define ENERGY_CUSTOMER_CODE "10004893"
#define ENERGY_COMMAND "OWNWaterElecService"
#define ENERGY_ROOM_VERIFY "101-5--73-517"
#define ENERGY_ELECTRICITY_BUSINESS_TYPE 0

// Keep automatic requests infrequent; the page also has a manual refresh.
#define ENERGY_REFRESH_INTERVAL_SECONDS (60 * 60)
