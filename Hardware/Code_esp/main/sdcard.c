#include "sdcard.h"

esp_vfs_fat_sdmmc_mount_config_t mount_config = {
	#ifdef CONFIG_EXAMPLE_FORMAT_IF_MOUNT_FAILED
			.format_if_mount_failed = true,
	#else
			.format_if_mount_failed = false,
	#endif // EXAMPLE_FORMAT_IF_MOUNT_FAILED
			.max_files = 10,
			.allocation_unit_size = 16 * 1024
};

sdmmc_card_t *card;

const char mount_point[] = MOUT_POINT;

sdmmc_host_t host = SDSPI_HOST_DEFAULT();

spi_bus_config_t bus_cfg = {
        .mosi_io_num = PIN_NUM_MOSI,
        .miso_io_num = PIN_NUM_MISO,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
};

esp_err_t sd_card_int(){
	esp_err_t ret;

	ret = spi_bus_initialize(host.slot, &bus_cfg, SDSPI_DEFAULT_DMA);
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "Failed to initialize bus.");
		return ESP_FAIL;
	}

	sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
	slot_config.gpio_cs = PIN_NUM_CS;
	slot_config.host_id = host.slot;

	ESP_LOGI(TAG, "Mounting filesystem");
	ret = esp_vfs_fat_sdspi_mount(mount_point, &host, &slot_config, &mount_config, &card);

	if (ret != ESP_OK) {
		if (ret == ESP_FAIL) {
			ESP_LOGE(TAG, "Failed to mount filesystem. "
					 "If you want the card to be formatted, set the CONFIG_EXAMPLE_FORMAT_IF_MOUNT_FAILED menuconfig option.");
		} else {
			ESP_LOGE(TAG, "Failed to initialize the card (%s). "
					 "Make sure SD card lines have pull-up resistors in place.", esp_err_to_name(ret));
		}
		return ret;
	}
	ESP_LOGI(TAG, "Filesystem mounted");

	// Card has been initialized, print its properties
	sdmmc_card_print_info(stdout, card);

	return ESP_OK;
};

esp_err_t sd_write_file(const char *nameFile, char *format,...){

    char pathFile[64];
    sprintf(pathFile, "%s/%s.txt", mount_point, nameFile);
	const char *pathFile1 = MOUT_POINT"/data.txt";


    ESP_LOGI(__func__, "Opening file %s...", pathFile1);
    FILE *file = fopen(pathFile, "a+");
    if (file == NULL)
    {
        ESP_LOGE(__func__, "Failed to open file for writing.");
        return ESP_FAIL;
    }

    char *dataString;
    int lenght;
    va_list argumentsList;
    va_list argumentsList_copy;
    va_start(argumentsList, format);
    va_copy(argumentsList_copy, argumentsList);
    lenght = vsnprintf(NULL, 0, format, argumentsList_copy);
    va_end(argumentsList_copy);

    dataString = (char*)malloc(++lenght);
    if(dataString == NULL) {
        ESP_LOGE(TAG, "Failed to create string data for writing.");
        va_end(argumentsList);
        return ESP_FAIL;
    }

    vsnprintf(dataString, (++lenght), format, argumentsList);
    ESP_LOGI(TAG, "Success to create string data(%d) for writing.", lenght);
    ESP_LOGI(TAG, "Writing data to file %s...", pathFile);
    ESP_LOGI(TAG, "%s;\n", dataString);

    int returnValue = 0;
    returnValue = fprintf(file, "%s", dataString);
    if (returnValue < 0)
    {
        ESP_LOGE(__func__, "Failed to write data to file %s.", pathFile);
        return ESP_FAIL;
    }
    ESP_LOGI(__func__, "Success to write data to file %s.", pathFile);
    fclose(file);
    va_end(argumentsList);
    free(dataString);
    return ESP_OK;
}
esp_err_t sd_read_file(const char *nameFile,const char *format,...){
	 char pathFile[64];
	    sprintf(pathFile, "%s/%s.txt", mount_point, nameFile);

	    ESP_LOGI(__func__, "Opening file %s...", pathFile);
	    FILE *file = fopen(pathFile, "r");
	    if (file == NULL)
	    {
	        ESP_LOGE(TAG, "Failed to open file for reading.");
	        return ESP_FAIL;
	    }

	    // Read a string data from file
	    char dataStr[256];
	    char *returnPtr;
	    returnPtr = fgets(dataStr, sizeof(dataStr), file);
	    fclose(file);

	    if (returnPtr == NULL)
	    {
	        ESP_LOGE(__func__, "Failed to read data from file %s.", pathFile);
	        return ESP_FAIL;
	    }

	    va_list argumentsList;
	    va_start(argumentsList, format);
	    int returnValue = 0;
	    returnValue = vsscanf(dataStr, format, argumentsList);
	    va_end(argumentsList);

	    if (returnValue < 0)
	    {
	        ESP_LOGE(__func__, "Failed to read data from file %s.", pathFile);
	        return ESP_FAIL;
	    }

	    return ESP_OK;
}
esp_err_t sd_rename_file(const char *oldNameFile, char *newNameFile){
	// Check if destination file exists before renaming
	    struct stat st;
	    if (stat(newNameFile, &st) == 0) {
	        ESP_LOGE(__func__, "File \"%s\" exists.", newNameFile);
	        return ESP_FAIL;
	    }

	    // Rename original file
	    ESP_LOGI(__func__, "Renaming file %s to %s", oldNameFile, newNameFile);
	    if (rename(oldNameFile, newNameFile) != 0)
	    {
	        ESP_LOGE(__func__, "Rename failed");
	        return ESP_FAIL;
	    } else {
	        ESP_LOGI(__func__, "Rename successful");
	        return ESP_OK;
	    }
}
esp_err_t sd_deinitialize(){
	ESP_LOGI(__func__, "Deinitializing SD card...");
	ESP_ERROR_CHECK_WITHOUT_ABORT(esp_vfs_fat_sdcard_unmount(mount_point,card));
	ESP_LOGI(__func__, "Card unmounted.");
	 ESP_ERROR_CHECK_WITHOUT_ABORT(spi_bus_free(host.slot));
	 return ESP_OK;
}
