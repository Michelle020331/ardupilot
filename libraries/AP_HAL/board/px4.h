#pragma once

#define HAL_BOARD_NAME "PX4"
#define HAL_CPU_CLASS HAL_CPU_CLASS_150
#define HAL_OS_POSIX_IO 1
#define HAL_BOARD_LOG_DIRECTORY "/fs/microsd/APM/LOGS"
#define HAL_BOARD_TERRAIN_DIRECTORY "/fs/microsd/APM/TERRAIN"
#define HAL_PARAM_DEFAULTS_PATH "/etc/defaults.parm"
#define HAL_INS_DEFAULT HAL_INS_PX4
#define HAL_BARO_DEFAULT HAL_BARO_PX4
#define HAL_COMPASS_DEFAULT HAL_COMPASS_PX4
#define HAL_SERIAL0_BAUD_DEFAULT 115200

#ifdef CONFIG_ARCH_BOARD_PX4FMU_V1
#define CONFIG_HAL_BOARD_SUBTYPE HAL_BOARD_SUBTYPE_PX4_V1
#define HAL_STORAGE_SIZE            8192
#elif defined(CONFIG_ARCH_BOARD_PX4FMU_V2)
#define CONFIG_HAL_BOARD_SUBTYPE HAL_BOARD_SUBTYPE_PX4_V2
#define HAL_STORAGE_SIZE            16384
#define HAL_HAVE_IMU_HEATER         1 // for Pixhawk2
#define HAL_IMU_TEMP_DEFAULT       -1 // disabled
#define HAL_WITH_UAVCAN             1
#define HAL_MINIMIZE_FEATURES       1
#elif defined(CONFIG_ARCH_BOARD_PX4FMU_V3)
#define CONFIG_HAL_BOARD_SUBTYPE HAL_BOARD_SUBTYPE_PX4_V3
#define HAL_STORAGE_SIZE            16384
#define HAL_HAVE_IMU_HEATER         1 // for Pixhawk2
#define HAL_IMU_TEMP_DEFAULT       -1 // disabled
#define HAL_WITH_UAVCAN             1
#elif defined(CONFIG_ARCH_BOARD_PX4FMU_V4)
#define CONFIG_HAL_BOARD_SUBTYPE HAL_BOARD_SUBTYPE_PX4_V4
#define HAL_STORAGE_SIZE            16384
#define HAL_WITH_UAVCAN             1
#else
#error "Unknown PX4 board type"
#endif

#define HAL_GPIO_A_LED_PIN        27
#define HAL_GPIO_B_LED_PIN        26
#define HAL_GPIO_C_LED_PIN        25
#define HAL_GPIO_LED_ON           LOW
#define HAL_GPIO_LED_OFF          HIGH

#define HAL_BARO_MS5611_NAME "ms5611"
#define HAL_BARO_MS5611_SPI_INT_NAME "ms5611_int"
#define HAL_BARO_MS5611_SPI_EXT_NAME "ms5611_ext"

#define HAL_INS_MPU60x0_NAME "mpu6000"
#define HAL_INS_MPU60x0_EXT_NAME "mpu6000_ext"

#define HAL_INS_LSM9DS0_G_NAME "lsm9ds0_g"
#define HAL_INS_LSM9DS0_A_NAME "lsm9ds0_am"

#define HAL_INS_LSM9DS0_EXT_G_NAME "lsm9ds0_ext_g"
#define HAL_INS_LSM9DS0_EXT_A_NAME "lsm9ds0_ext_am"

#define HAL_INS_MPU9250_NAME "mpu9250"
#define HAL_INS_MPU9250_EXT_NAME "mpu9250_ext"

#define HAL_INS_MPU6500_NAME "mpu6500"

#define HAL_INS_ICM20608_NAME "icm20608"
#define HAL_INS_ICM20608_AM_NAME "icm20608-am"
#define HAL_INS_ICM20608_EXT_NAME "icm20608_ext"

#define HAL_COMPASS_HMC5843_NAME "hmc5843"

#ifdef CONFIG_ARCH_BOARD_PX4FMU_V1
#define HAL_BARO_MS5611_I2C_ADDR 0x76
#define HAL_BARO_MS5611_I2C_BUS  0
#endif

