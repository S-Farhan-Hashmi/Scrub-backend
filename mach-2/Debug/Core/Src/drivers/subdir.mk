################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Core/Src/drivers/compass.c \
../Core/Src/drivers/dht11.c \
../Core/Src/drivers/gps_nmea.c 

OBJS += \
./Core/Src/drivers/compass.o \
./Core/Src/drivers/dht11.o \
./Core/Src/drivers/gps_nmea.o 

C_DEPS += \
./Core/Src/drivers/compass.d \
./Core/Src/drivers/dht11.d \
./Core/Src/drivers/gps_nmea.d 


# Each subdirectory must supply rules for building sources it contributes
Core/Src/drivers/%.o Core/Src/drivers/%.su Core/Src/drivers/%.cyclo: ../Core/Src/drivers/%.c Core/Src/drivers/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m4 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32F446xx -c -I../Core/Inc -I../Drivers/STM32F4xx_HAL_Driver/Inc -I../Drivers/STM32F4xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32F4xx/Include -I../Drivers/CMSIS/Include -I../FATFS/Target -I../FATFS/App -I../Middlewares/Third_Party/FreeRTOS/Source/include -I../Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2 -I../Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM4F -I../Middlewares/Third_Party/FatFs/src -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Core-2f-Src-2f-drivers

clean-Core-2f-Src-2f-drivers:
	-$(RM) ./Core/Src/drivers/compass.cyclo ./Core/Src/drivers/compass.d ./Core/Src/drivers/compass.o ./Core/Src/drivers/compass.su ./Core/Src/drivers/dht11.cyclo ./Core/Src/drivers/dht11.d ./Core/Src/drivers/dht11.o ./Core/Src/drivers/dht11.su ./Core/Src/drivers/gps_nmea.cyclo ./Core/Src/drivers/gps_nmea.d ./Core/Src/drivers/gps_nmea.o ./Core/Src/drivers/gps_nmea.su

.PHONY: clean-Core-2f-Src-2f-drivers

