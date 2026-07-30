#include "nvs.h"
#include "oxiXesp.h"

#define SWDIO 18
#define SWCLK 19
#define CSW_DEB_PRIV 0xFF000040
#define CSW_SIZE_WORD 2
#define DHCSR 0xE000EDF0 //Debug Halting Control and Status register
#define DBGKEY 0xA05F0000 //Debug Key. Must be written whenever this register is written
#define C_DEBUGEN 0x00000001 //Enables debug flag in the DHCSR
#define C_HALT 0x00000002 //command HALT flag in the DHCSR
#define S_HALT 0x00020000 //status HALT flag in the DHCSR
#define DBGMCU_CR 0xE0042004 //Debug MCU configuration register
#define DBG_IWDG_STOP 0x100 //Debug independent watchdog stopped when core is halted
#define CSW_INC_SINGLE 0x10
#define REQ_AP_READ 0x9F
#define FLASH_CR_PG 1 //Flash programming chosen
#define FLASH_CR 0x40022010 //Flash control register
#define CSW_INC_PACKED 0x20
#define CSW_SIZE_HALFWORD 1
#define FLASH_SR 0x4002200C //Flash status register
#define FLASH_SR_PGERR 4 //Set by hardware when an address don't contents 0xFFFF
#define FLASH_SR_WRPRTERR 0x10 //Set by hardware when programming a write-protected
#define STM_FLASH_KEY1 0x45670123
#define STM_FLASH_KEY2 0xCDEF89AB
#define FLASH_KEYR 0x40022004 //FPEC key register
#define FLASH_CR_LOCK 0x80 //indicates that the FPEC and FLASH_CR are locked
#define FLASH_SR_BSY 1 //This indicates that a Flash operation is in progress
#define FLASH_CR_PER 2 //Page Erase chosen
#define FLASH_AR 0x40022014 //Flash address register
#define FLASH_CR_STRT 0x40 //This bit triggers an ERASE operation when set
#define VECTKEY 0x05FA0000 //Register key of the SCB_AIRCR
#define SYSRESETREQ 0x00000004 //System reset request
#define SCB_AIRCR 0xE000ED0C //Application interrupt and reset control register

void ota_err_msg(uint8_t er)
{
	printf("%s%s%s%02d\n", TAG_OXI, OXI_ERR, OTA_TAG, er);
}

bool swd_init(swd_info_t *swdinf)
{
	esp_err_t er;
	
	spi_bus_config_t pinsSPI = {
		.mosi_io_num = SWDIO,
		.miso_io_num = -1,
		.sclk_io_num = SWCLK,
		.quadwp_io_num = -1,
		.quadhd_io_num = -1,
		.max_transfer_sz = 0
	};
	spi_device_interface_config_t confSPI = {
		.command_bits = 0,
		.address_bits = 0,
		.dummy_bits = 0,
		.mode = 0,
		.duty_cycle_pos = 0,
		.cs_ena_pretrans = 0,
		.cs_ena_posttrans = 0,
		.clock_speed_hz = 1000000,
		.spics_io_num = -1,
		.flags = SPI_DEVICE_3WIRE | SPI_DEVICE_HALFDUPLEX |
													SPI_DEVICE_BIT_LSBFIRST,
		.queue_size = 24,
		.pre_cb = NULL,
		.post_cb = NULL
	};
	if ((er = spi_bus_initialize(SPI2_HOST, &pinsSPI, 0)) != ESP_OK) {
		//oxi_err_check("SWD SPI bus init", er);
		return false;
	}
	swdinf->spi_bus_init = true;
	if ((er = spi_bus_add_device(SPI2_HOST, &confSPI, &(swdinf->deviceSWD)))) {
		//oxi_err_check("SWD SPI ADD DEVICE", er);
		return false;
	}
	if ((swdinf->libswdctx = libswd_init()) == NULL) {
		//oxi_err_check("LibSWD init", -1);
		return false;
	}
	libswd_log_level_set(swdinf->libswdctx, LIBSWD_LOGLEVEL_DEBUG);
	/* Connect ESP SPI driver to LibSWD */
	swdinf->libswdctx->driver->device = &(swdinf->deviceSWD);

	/* Read Debug Port ID code for unlock SWD interface */
	int idcode = 0;
	int *p_idcode = &idcode;
	int dap_res = libswd_dap_detect(swdinf->libswdctx, LIBSWD_OPERATION_EXECUTE,
																	&p_idcode);
	if (dap_res != LIBSWD_OK) {
		//oxi_err_check("DAP detect", dap_res);
		return false;
	}
	//printf("Detected IDCODE: 0x%08X\n", *p_idcode);
	er = libswd_memap_init(swdinf->libswdctx, LIBSWD_OPERATION_EXECUTE);
	if (er != LIBSWD_OK) {
		//oxi_err_check("MEMAP init", -1);
		return false;
	}
	/* set AHB-AP CSW flags: Master type - debug and privilege control */
	/*if (csw_set_privilege(swdinf->libswdctx, CSW_SIZE_WORD) < 0) {
		oxi_err_check("SWD CSW set", -1);
		return false;
	}*/
	if (libswd_memap_setup(swdinf->libswdctx, LIBSWD_OPERATION_EXECUTE,
									CSW_DEB_PRIV + CSW_SIZE_WORD, DHCSR) < 0) {
		//oxi_err_check("SWD CSW set", -1);
		return false;
	}
	//printf("%sSWD CSW is set\n", TAG_OXI);
	/* Enable debug and command HALT for STM32 */
	swdinf->adr_swd = DHCSR;
	swdinf->dat_swd[0] = DBGKEY | C_DEBUGEN | C_HALT;
	if (libswd_memap_write_int(swdinf->libswdctx, LIBSWD_OPERATION_EXECUTE,
									swdinf->adr_swd, 1, swdinf->dat_swd) < 0) {
		//oxi_err_check("DHCSR write", -1);
		return false;
	}
	/* Wait STM32 HALT state */
	swdinf->dat_swd[1] = 0;
	do {
		libswd_memap_read_int(swdinf->libswdctx, LIBSWD_OPERATION_EXECUTE,
										swdinf->adr_swd, 1, swdinf->dat_swd);
		++(swdinf->dat_swd[1]);
	} while (!(swdinf->dat_swd[0] & S_HALT || swdinf->dat_swd[1] > 9));
	if (swdinf->dat_swd[1] > 9) {
		//oxi_err_check("STM32 not halted", -1);
		return false;
	}
	/* stop independed watchdog */
	swdinf->dat_swd[0] = DBG_IWDG_STOP;
	libswd_memap_write_int(swdinf->libswdctx, LIBSWD_OPERATION_EXECUTE,
												DBGMCU_CR, 1, swdinf->dat_swd);
	return true;
}

static int stm_check_erase_pg(swd_info_t *swdinf, int pg_adr)
{
	if (libswd_memap_read_int(swdinf->libswdctx, LIBSWD_OPERATION_EXECUTE,
											pg_adr, 4, swdinf->dat_swd) < 0) {
		oxi_err_check("STM flash read", -1);
		return -1;
	}
	if ((swdinf->dat_swd[0] & swdinf->dat_swd[1] & swdinf->dat_swd[2] &
											swdinf->dat_swd[3]) == 0xFFFFFFFF)
		return 1;
	return 0;
}

int get_size_stm_prog_old(swd_info_t *swdinf)
{
	int offset = 0;
	//nvs_handle_t stm_nvs;

	int flash_free = stm_check_erase_pg(swdinf, BASE_STM_FLASH + offset);
	while (flash_free != 1 && offset / STM_PAGE_SIZE < STM_END_PG) {
		if (flash_free == -1) {
			//oxi_err_check("STM check erase page", -1);
			return -1;
		}
		offset += STM_PAGE_SIZE;
	}
	/* save backup size for possibility downgrade */
	/*if (nvs_open("stm_nvs", NVS_READWRITE, &stm_nvs) +
								nvs_set_i32(stm_nvs, "stm_old_size", offset) +
								nvs_commit(stm_nvs) != ESP_OK) {
		//oxi_err_check("Save backup size", -1);
		offset = -1;
	}
	nvs_close(stm_nvs);*/
	return offset;
}

/**
 * @brief Read int32 data from AP
 * ORUNDETECT bit in CTRL/STAT register must be reset for right handle ACK WAIT
 * Parity don't check
 * 
 * @param swdctx libswd context
 * @param dat pointer for read data
 * @return char ACK value
 */
static char readAP(libswd_ctx_t *swdctx, int *dat)
{
	char reqApRd = REQ_AP_READ;
	char ack = 0;
	char par = 0;

	do {
		libswd_drv_mosi_8(swdctx, NULL, &reqApRd, LIBSWD_REQUEST_BITLEN, 0);
		libswd_drv_mosi_trn(swdctx, LIBSWD_TURNROUND_1_VAL);
		libswd_drv_miso_8(swdctx, NULL, &ack, LIBSWD_ACK_BITLEN, 0);
		if (ack == LIBSWD_ACK_WAIT_VAL)
			libswd_drv_miso_trn(swdctx, LIBSWD_TURNROUND_1_VAL);
	} while (ack == LIBSWD_ACK_WAIT_VAL);
	if (ack == LIBSWD_ACK_OK_VAL) {
		libswd_drv_miso_32(swdctx, NULL, dat, LIBSWD_DATA_BITLEN, 0);
		libswd_drv_miso_8(swdctx, NULL, &par, 1, 0);
	}
	libswd_drv_miso_trn(swdctx, LIBSWD_TURNROUND_1_VAL);
	return ack;
}

int save_stm_range(upgrade_info *upinf, int size)
{
	char *buf;
	nvs_handle_t stm_nvs;
	int ret_val;

	/* Size of the STM32 software for backup reset to zero for determinate
	unsuccessfull save old software */
	ret_val = nvs_open("stm_nvs", NVS_READWRITE, &stm_nvs) +
								nvs_set_i32(stm_nvs, "stm_old_size", 0) +
								nvs_commit(stm_nvs);
	nvs_close(stm_nvs);
	if (ret_val != ESP_OK)
		return -1;

	/* define erase range */
	size_t erase_range = (size / upinf->stm_part->erase_size) *
												upinf->stm_part->erase_size;
	if (erase_range < size)
		erase_range += upinf->stm_part->erase_size;
	/* erase ESP flash */
	if (esp_partition_erase_range(upinf->stm_part, 0, erase_range)) {
		//oxi_err_check("ESP erase", -1);
		return -1;
	}
	/* memory for buffer write data */
	if ((buf = malloc(STM_PAGE_SIZE)) == NULL)
		return -1;
	/* Reset ORUNDETECT bit in CTRL/STAT register */
	int *pint;
	libswd_dp_read(upinf->swdinf.libswdctx, LIBSWD_OPERATION_EXECUTE,
												LIBSWD_DP_CTRLSTAT_ADDR, &pint);
	*pint &= ~LIBSWD_DP_CTRLSTAT_ORUNDETECT;
	libswd_dp_write(upinf->swdinf.libswdctx, LIBSWD_OPERATION_EXECUTE,
												LIBSWD_DP_CTRLSTAT_ADDR, pint);
	/* set autoincrement */
	if (libswd_memap_setup(upinf->swdinf.libswdctx, LIBSWD_OPERATION_EXECUTE,
								CSW_DEB_PRIV | CSW_INC_SINGLE | CSW_SIZE_WORD,
								BASE_STM_FLASH) != LIBSWD_OK) {
		free(buf);
		return -1;
	}
	/* read program from STM and save to ESP */
	int binsize = 0;
	int piece = STM_PAGE_SIZE;
	int dt;
	libswd_drv_miso_trn(upinf->swdinf.libswdctx, LIBSWD_TURNROUND_1_VAL);

	while (binsize < size) {
		if (size - binsize < piece)
			piece = size - binsize;
		/* clean SWD read buffer from previous value */
		if (readAP(upinf->swdinf.libswdctx, &dt) != LIBSWD_ACK_OK_VAL) {
			free(buf);
			return -1;
		}
		for (int i = 0; i < piece; i+=4) {
////////////////////////////////////////////////////////////////////////////////
			/*//swd_prnt = true;
			if (libswd_ap_read(upinf->swdinf.libswdctx,
			LIBSWD_OPERATION_EXECUTE, LIBSWD_MEMAP_DRW_ADDR, &pint) < 0) {
				free(buf);
				return -1;
			}
			//swd_prnt = false;*/
////////////////////////////////////////////////////////////////////////////////
			if (readAP(upinf->swdinf.libswdctx, &dt) != LIBSWD_ACK_OK_VAL) {
				free(buf);
				return -1;
			}
			*((int *)(buf + i)) = dt;
		}
		//printf("%sSTM read %d bytes\n", TAG_OXI, binsize + piece);
		if (esp_partition_write_raw(upinf->stm_part, binsize,
											buf, piece) != ESP_OK) {
			//oxi_err_check("ESP save", -1);
			free(buf);
			return -1;
		}
		binsize += piece;
		/* set next chunk address */
		dt = BASE_STM_FLASH + binsize;
		if (libswd_ap_write(upinf->swdinf.libswdctx, LIBSWD_OPERATION_EXECUTE,
		LIBSWD_MEMAP_TAR_ADDR, &dt) < 0) {
			free(buf);
			return -1;
		}
		printf("%s%sOld saved %d bytes\n", TAG_OXI, SWD_TAG, binsize);
	}
	upinf->swdinf.dat_swd[0] = CSW_DEB_PRIV | CSW_SIZE_WORD;
	libswd_ap_write(upinf->swdinf.libswdctx, LIBSWD_OPERATION_EXECUTE,
								LIBSWD_MEMAP_CSW_ADDR, upinf->swdinf.dat_swd);
	free(buf);
	/* Size of the STM32 software for backup reset to zero for determinate
	unsuccessfull save old software */
	ret_val =	nvs_open("stm_nvs", NVS_READWRITE, &stm_nvs) +
				nvs_set_i32(stm_nvs, "stm_old_size", binsize) +
				nvs_commit(stm_nvs);
	nvs_close(stm_nvs);
	if (ret_val != ESP_OK)
		return -1;
	return binsize;
}

static bool stm_flash_unlock(swd_info_t *swdinf)
{
	/* unlock STM flash */
	swdinf->dat_swd[0] = STM_FLASH_KEY1;
	if (libswd_memap_write_int(swdinf->libswdctx,
				LIBSWD_OPERATION_EXECUTE, FLASH_KEYR, 1, swdinf->dat_swd) < 0) {
		oxi_err_check("STM flash unlock", -1);
		return false;
	}
	swdinf->dat_swd[0] = STM_FLASH_KEY2;
	if (libswd_memap_write_int(swdinf->libswdctx,
				LIBSWD_OPERATION_EXECUTE, FLASH_KEYR, 1, swdinf->dat_swd) < 0) {
		oxi_err_check("STM flash unlock", -1);
		return false;
	}
	/* Check that Flash memory is unlocked */
	if (libswd_memap_read_int(swdinf->libswdctx,
				LIBSWD_OPERATION_EXECUTE, FLASH_CR, 1, swdinf->dat_swd) < 0) {
		oxi_err_check("STM read FLASH_CR", -1);
		return false;
	}
	if (swdinf->dat_swd[0] & FLASH_CR_LOCK)
		return -1;
	return true;
}

static bool stm_set_bit(swd_info_t *swdinf, int reg_adr, int bit_mask)
{
	if (libswd_memap_read_int(swdinf->libswdctx, LIBSWD_OPERATION_EXECUTE,
											reg_adr, 1, swdinf->dat_swd) < 0) {
		oxi_err_check("STM read register", -1);
		return false;
	}
	swdinf->dat_swd[0] |= bit_mask;
	if (libswd_memap_write_int(swdinf->libswdctx, LIBSWD_OPERATION_EXECUTE,
											reg_adr, 1, swdinf->dat_swd) < 0) {
		oxi_err_check("STM write register", -1);
		return false;
	}
	return true;
}

static bool stm_erase_range(swd_info_t *swdinf, int size)
{
	/* Check that no Flash memory operation is ongoing by checking the BSY
	** bit in the FLASH_SR register */
	do {
		if (libswd_memap_read_int(swdinf->libswdctx,
				LIBSWD_OPERATION_EXECUTE, FLASH_SR, 1, swdinf->dat_swd) < 0) {
			oxi_err_check("STM read FLASH_SR", -1);
			return false;
		}
	} while (swdinf->dat_swd[0] & FLASH_SR_BSY);
	/* Set the PER bit in the FLASH_CR register */
	if (!stm_set_bit(swdinf, FLASH_CR, FLASH_CR_PER)) {
		oxi_err_check("STM set PER bit in FLASH_CR", -1);
		return false;
	}
	/* define number of the STM flash pages for clean */
	int pg_num = size / STM_PAGE_SIZE;
	if (pg_num * STM_PAGE_SIZE < size)
		pg_num++;
	/*if (upgr_ctrl_type == UPGR_CTRL_FULL)
		pg_num = STM_BEGIN_PARAM_PG + STM_PARAMETER_PAGES;*/
	/* erase STM flash pages */
	int check_res;
	int pgadr;
	for (int i = 0; i < pg_num; i++) {
		/* Program the FLASH_AR register to select a page to erase */
		pgadr = swdinf->dat_swd[0] = BASE_STM_FLASH + i * STM_PAGE_SIZE;
		if (libswd_memap_write_int(swdinf->libswdctx, LIBSWD_OPERATION_EXECUTE,
										FLASH_AR, 1, swdinf->dat_swd) < 0) {
			oxi_err_check("STM write FLASH_AR", -1);
			return false;
		}
		/* Set the STRT bit in the FLASH_CR register */
		if (!stm_set_bit(swdinf, FLASH_CR, FLASH_CR_STRT)) {
			oxi_err_check("STM set STRT bit in FLASH_CR", -1);
			return false;
		}
		/* Wait for the BSY bit to be reset */
		do {
			if (libswd_memap_read_int(swdinf->libswdctx, 
			LIBSWD_OPERATION_EXECUTE, FLASH_SR, 1, swdinf->dat_swd) < 0) {
				oxi_err_check("STM read FLASH_SR", -1);
				return false;
			}
		} while (swdinf->dat_swd[0] & FLASH_SR_BSY);
		/* Read the erased page and verify */
		if ((check_res = stm_check_erase_pg(swdinf, pgadr)) != 1) {
			oxi_err_check("STM page erase", -1);
			return false;
		}
		printf("%sSTM page %d address 0x%08X erased\n", TAG_OXI, i, pgadr);
	}
	/* PER clear*/
	swdinf->dat_swd[0] = 0;
	if (libswd_memap_write_int(swdinf->libswdctx, LIBSWD_OPERATION_EXECUTE,
										FLASH_CR, 1, swdinf->dat_swd) < 0) {
		oxi_err_check("STM clear PER bit in FLASH_CR", -1);
		return false;
	}
	printf("%sSTM erased\n", TAG_OXI);
	return true;
}

static bool stm_flash_lock(swd_info_t *swdinf)
{
	swdinf->dat_swd[0] = FLASH_CR_LOCK;
	if (libswd_memap_write_int(swdinf->libswdctx, LIBSWD_OPERATION_EXECUTE,
										FLASH_CR, 1, swdinf->dat_swd) < 0) {
		oxi_err_check("STM flash lock", -1);
		return false;
	}
	return true;
}

int write_stm(upgrade_info *upinf, int stm_adr, int offset, int size)
{
	if (!stm_flash_unlock(&(upinf->swdinf))) {
		//oxi_err_check("STM flash isn't unlocked", -1);
		return -1;
	}
	if (!stm_erase_range(&(upinf->swdinf), size)) {
		//oxi_err_check("STM erase range", -1);
		stm_flash_lock(&(upinf->swdinf));
		return -1;
	}
	/* set PG (ProGram) bit in FLASH_CR register */
	upinf->swdinf.dat_swd[0] = FLASH_CR_PG;
	if (libswd_memap_write_int(upinf->swdinf.libswdctx,
										LIBSWD_OPERATION_EXECUTE, FLASH_CR, 1,
										upinf->swdinf.dat_swd) < 0) {
		//oxi_err_check("STM set PG bit in FLASH_CR", -1);
		stm_flash_lock(&(upinf->swdinf));
		return -1;
	}
	/* cycle write to STM flash by chunk */
	int binsize = 0;
	int piece = STM_PAGE_SIZE < upinf->chunk ? STM_PAGE_SIZE : upinf->chunk;
	int *pint;
	while (binsize < size) {
		if (size - binsize < STM_PAGE_SIZE)
			piece = size - binsize;
		if (esp_partition_read_raw(upinf->stm_part, offset + binsize,
											upinf->data, piece) != ESP_OK) {
			//oxi_err_check("ESP read", -1);
			stm_flash_lock(&(upinf->swdinf));
			return -1;
		}
		/* set CSW memory access size to HalfWord and TAR to current piece */
		if (libswd_memap_setup(upinf->swdinf.libswdctx,
							LIBSWD_OPERATION_EXECUTE,
							CSW_DEB_PRIV | CSW_INC_PACKED | CSW_SIZE_HALFWORD,
							stm_adr + binsize) < 0) {
			stm_flash_lock(&(upinf->swdinf));
			return -1;
		}
		/* cycle write STM flash by 4 bytes*/
		for (int i = 0; i < piece; i += 4) {
			if (libswd_ap_write(upinf->swdinf.libswdctx,
							LIBSWD_OPERATION_EXECUTE, LIBSWD_MEMAP_DRW_ADDR,
							(int *)(upinf->data + i)) < 0) {
				stm_flash_lock(&(upinf->swdinf));
				return -1;
			}
		}
		/* check flash program errors */
		if (libswd_memap_read_int_csw(upinf->swdinf.libswdctx,
		LIBSWD_OPERATION_EXECUTE, FLASH_SR, 1,upinf->swdinf.dat_swd,
		CSW_DEB_PRIV | CSW_SIZE_WORD) < 0 ||
		upinf->swdinf.dat_swd[0] & (FLASH_SR_PGERR | FLASH_SR_WRPRTERR)) {
			stm_flash_lock(&(upinf->swdinf));
			return -1;
		}
		/* Reset ORUNDETECT bit in CTRL/STAT register */
		libswd_dp_read(upinf->swdinf.libswdctx, LIBSWD_OPERATION_EXECUTE,
												LIBSWD_DP_CTRLSTAT_ADDR, &pint);
		*pint &= ~LIBSWD_DP_CTRLSTAT_ORUNDETECT;
		libswd_dp_write(upinf->swdinf.libswdctx, LIBSWD_OPERATION_EXECUTE,
												LIBSWD_DP_CTRLSTAT_ADDR, pint);
		/* Verife new software write */
		if (libswd_memap_setup(upinf->swdinf.libswdctx,
								LIBSWD_OPERATION_EXECUTE,
								CSW_DEB_PRIV | CSW_INC_SINGLE | CSW_SIZE_WORD,
								stm_adr + binsize) < 0) {
			stm_flash_lock(&(upinf->swdinf));
			return -1;
		}
		/* clean SWD read buffer from previous value */
		int dt;
		libswd_drv_miso_trn(upinf->swdinf.libswdctx, LIBSWD_TURNROUND_1_VAL);
		if (readAP(upinf->swdinf.libswdctx, &dt) != LIBSWD_ACK_OK_VAL) {
			stm_flash_lock(&(upinf->swdinf));
			return -1;
		}
		// Read and verife cycle
		for (int i = 0; i < piece; i+=4) {
			if (readAP(upinf->swdinf.libswdctx, &dt) != LIBSWD_ACK_OK_VAL) {
				stm_flash_lock(&(upinf->swdinf));
				return -1;
			}
			if (dt != *((int *)(upinf->data + i))) {
				stm_flash_lock(&(upinf->swdinf));
				return -1;
			}
		}
		binsize += piece;
		printf("%sSTM write %d bytes\n", TAG_OXI, binsize);
	}
	/* lock STM flash */
	if (!stm_flash_lock(&(upinf->swdinf)))
		return -1;

	return binsize;
}

void restartSTM(upgrade_info *upinf)
{
	/* restart STM */
	upinf->swdinf.dat_swd[0] = VECTKEY | SYSRESETREQ;
	if (libswd_memap_write_int(upinf->swdinf.libswdctx,
	LIBSWD_OPERATION_EXECUTE, SCB_AIRCR, 1, upinf->swdinf.dat_swd) < 0) {
		//oxi_err_check("STM reset request", -1);
		return;
	}
	/* STM run for reset */
	upinf->swdinf.dat_swd[0] = DBGKEY;
	if (libswd_memap_write_int(upinf->swdinf.libswdctx,
	LIBSWD_OPERATION_EXECUTE, DHCSR, 1, upinf->swdinf.dat_swd) < 0) {
		//oxi_err_check("STM reset and start", -1);
		return;
	}
}
