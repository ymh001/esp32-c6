#include "board_sd.h"
#include "board.h"
#include "sd_files.h"
#include "driver/gpio.h"
#include "driver/sdspi_host.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "esp_log.h"
#include "esp_random.h"
#include <dirent.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>

static constexpr const char *TAG="sdcard";
static constexpr const char *ROOT="/sdcard";
static sdmmc_card_t *card;
static bool transfer_guard;
bool board_sd_is_mounted(){return card!=nullptr;}
void board_sd_transfer_guard(bool active){transfer_guard=active;}
static esp_err_t last_error=ESP_ERR_INVALID_STATE;
// Reject invalid identification data before IDF derives a zero-sized disk.
// No protocol checks, command errors or CRC errors are suppressed.
static esp_err_t checked_transaction(int slot,sdmmc_command_t *cmd) {
    const esp_err_t err=sdspi_host_do_transaction(slot,cmd);
    if(err!=ESP_OK)return err;
    if((cmd->opcode==9 || cmd->opcode==10) && cmd->data && cmd->datalen==16) {
        const auto bytes=static_cast<const uint8_t *>(cmd->data);
        bool all_zero=true,all_ff=true;
        for(size_t i=0;i<16;++i){all_zero &= bytes[i]==0;all_ff &= bytes[i]==0xff;}
        if(all_zero || all_ff) {
            ESP_LOGE(TAG,"Invalid SD %s: all-%s register; refusing filesystem access",
                     cmd->opcode==9?"CSD":"CID",all_zero?"zero":"FF");
            return ESP_ERR_INVALID_RESPONSE;
        }
    }
    return ESP_OK;
}
static esp_err_t drain_lcd() {
    auto display=board_display();
    // Command -1 with no parameters only drains pending LCD DMA in IDF 5.5.2.
    return display && display->panel_io ?
        esp_lcd_panel_io_tx_param(display->panel_io,-1,nullptr,0) : ESP_OK;
}
esp_err_t board_sd_mount() {
    if(card)return ESP_OK;
    esp_err_t err=drain_lcd();if(err!=ESP_OK)return last_error=err;
    sdmmc_host_t host=SDSPI_HOST_DEFAULT();
    host.slot=SPI2_HOST;
    host.max_freq_khz=1000; // Conservative clock on the shared QSPI wiring.
    host.do_transaction=checked_transaction;
    sdspi_device_config_t device=SDSPI_DEVICE_CONFIG_DEFAULT();
    device.host_id=SPI2_HOST;device.gpio_cs=(gpio_num_t)BOARD_SD_CS_GPIO;
    esp_vfs_fat_mount_config_t mount={};
    mount.format_if_mount_failed=false;mount.max_files=3;mount.allocation_unit_size=0;
    err=esp_vfs_fat_sdspi_mount(ROOT,&host,&device,&mount,&card);
    last_error=err;
    if(err!=ESP_OK){
        card=nullptr;
        // Failure cleanup detaches this device, never the shared SPI bus.
        gpio_set_direction((gpio_num_t)BOARD_SD_CS_GPIO,GPIO_MODE_OUTPUT);
        gpio_set_level((gpio_num_t)BOARD_SD_CS_GPIO,1);
        ESP_LOGW(TAG,"Not mounted: %s. Card absent, I/O error or unsupported filesystem; no format performed",esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG,"Mounted FAT at %s, SPI2 %d kHz, CS=%d",ROOT,host.max_freq_khz,BOARD_SD_CS_GPIO);
    sdmmc_card_print_info(stdout,card);
    board_sd_status();return ESP_OK;
}
esp_err_t board_sd_unmount() {
    if(transfer_guard){ESP_LOGW(TAG,"Close file transfer before unmounting");return ESP_ERR_INVALID_STATE;}
    if(!card)return ESP_OK;
    esp_err_t err=drain_lcd();if(err!=ESP_OK)return last_error=err;
    // IDF frees the card before unregistering VFS, which can itself fail.
    auto mounted=card;card=nullptr;
    err=esp_vfs_fat_sdcard_unmount(ROOT,mounted);last_error=err;
    gpio_set_direction((gpio_num_t)BOARD_SD_CS_GPIO,GPIO_MODE_OUTPUT);
    gpio_set_level((gpio_num_t)BOARD_SD_CS_GPIO,1);
    ESP_LOGI(TAG,"Unmount: %s",esp_err_to_name(err));return err;
}
void board_sd_status() {
    if(!card){ESP_LOGI(TAG,"mounted=0 last_error=%s",esp_err_to_name(last_error));return;}
    uint64_t total=0,free=0;esp_err_t err=esp_vfs_fat_info(ROOT,&total,&free);
    ESP_LOGI(TAG,"mounted=1 card=%s capacity_bytes=%llu filesystem_bytes=%llu free_bytes=%llu info=%s",
             card->cid.name,(unsigned long long)card->csd.capacity*card->csd.sector_size,
             (unsigned long long)total,(unsigned long long)free,esp_err_to_name(err));
}
void board_sd_list() {
    if(!card){board_sd_status();return;}
    DIR *dir=opendir(ROOT);if(!dir){ESP_LOGW(TAG,"List failed: %s",strerror(errno));return;}
    unsigned count=0;
    for(;;){
        errno=0;auto entry=readdir(dir);
        if(!entry){if(errno)ESP_LOGW(TAG,"Directory read failed: %s",strerror(errno));break;}
        if(!strcmp(entry->d_name,".") || !strcmp(entry->d_name,".."))continue;
        if(count++>=64){ESP_LOGI(TAG,"Listing limited to 64 entries");break;}
        char full[sd_files::path_size];struct stat st={};
        if(sd_files::path(ROOT,entry->d_name,full,sizeof(full)) || stat(full,&st)){
            ESP_LOGI(TAG,"? %s",entry->d_name);continue;
        }
        ESP_LOGI(TAG,"%s %lld %s",S_ISDIR(st.st_mode)?"DIR":"FILE",(long long)st.st_size,entry->d_name);
    }
    closedir(dir);ESP_LOGI(TAG,"Directory listing complete");
}
int board_sd_read(const char *relative,void *data,size_t capacity,size_t *size) {
    if(transfer_guard){if(size)*size=0;return EBUSY;}
    if(!card){if(size)*size=0;return ENODEV;}
    return sd_files::read(ROOT,relative,data,capacity,size);
}
int board_sd_write_new(const char *relative,const void *data,size_t size) {
    if(transfer_guard)return EBUSY;
    return card?sd_files::write_new(ROOT,relative,data,size):ENODEV;
}
void board_sd_self_test() {
    if(transfer_guard){ESP_LOGW(TAG,"Close file transfer before running SD self-test");return;}
    if(!card){board_sd_status();return;}
    char name[16];int err=sd_files::self_test(ROOT,esp_random(),name,sizeof(name));
    if(err)ESP_LOGE(TAG,"SELFTEST FAIL file=%s error=%s",name,strerror(err));
    else ESP_LOGI(TAG,"SELFTEST PASS: 8192 bytes written, synced, closed, reopened, verified and removed (%s)",name);
}
