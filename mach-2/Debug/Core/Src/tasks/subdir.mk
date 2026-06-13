################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Core/Src/tasks/task_compass.c \
../Core/Src/tasks/task_debug.c \
../Core/Src/tasks/task_flysky.c \
../Core/Src/tasks/task_gps.c \
../Core/Src/tasks/task_motor.c \
../Core/Src/tasks/task_rpi.c \
../Core/Src/tasks/task_sdlog.c \
../Core/Src/tasks/task_sensors.c 

OBJS += \
./Core/Src/tasks/task_compass.o \
./Core/Src/tasks/task_debug.o \
./Core/Src/tasks/task_flysky.o \
./Core/Src/tasks/task_gps.o \
./Core/Src/tasks/task_motor.o \
./Core/Src/tasks/task_rpi.o \
./Core/Src/tasks/task_sdlog.o \
./Core/Src/tasks/task_sensors.o 

C_DEPS += \
./Core/Src/tasks/task_compass.d \
./Core/Src/tasks/task_debug.d \
./Core/Src/tasks/task_flysky.d \
./Core/Src/tasks/task_gps.d \
./Core/Src/tasks/task_motor.d \
./Core/Src/tasks/task_rpi.d \
./Core/Src/tasks/task_sdlog.d \
./Core/Src/tasks/task_sensors.d 


# Each subdirectory must supply rules for building sources it contributes
Core/Src/tasks/%.o Core/Src/tasks/%.su Core/Src/tasks/%.cyclo: ../Core/Src/tasks/%.c Core/Src/tasks/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m4 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32F446xx -c -I../Core/Inc -I../Drivers/STM32F4xx_HAL_Driver/Inc -I../Drivers/STM32F4xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32F4xx/Include -I../Drivers/CMSIS/Include -I../FATFS/Target -I../FATFS/App -I../Middlewares/Third_Party/FreeRTOS/Source/include -I../Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2 -I../Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM4F -I../Middlewares/Third_Party/FatFs/src -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Core-2f-Src-2f-tasks

clean-Core-2f-Src-2f-tasks:
	-$(RM) ./Core/Src/tasks/task_compass.cyclo ./Core/Src/tasks/task_compass.d ./Core/Src/tasks/task_compass.o ./Core/Src/tasks/task_compass.su ./Core/Src/tasks/task_debug.cyclo ./Core/Src/tasks/task_debug.d ./Core/Src/tasks/task_debug.o ./Core/Src/tasks/task_debug.su ./Core/Src/tasks/task_flysky.cyclo ./Core/Src/tasks/task_flysky.d ./Core/Src/tasks/task_flysky.o ./Core/Src/tasks/task_flysky.su ./Core/Src/tasks/task_gps.cyclo ./Core/Src/tasks/task_gps.d ./Core/Src/tasks/task_gps.o ./Core/Src/tasks/task_gps.su ./Core/Src/tasks/task_motor.cyclo ./Core/Src/tasks/task_motor.d ./Core/Src/tasks/task_motor.o ./Core/Src/tasks/task_motor.su ./Core/Src/tasks/task_rpi.cyclo ./Core/Src/tasks/task_rpi.d ./Core/Src/tasks/task_rpi.o ./Core/Src/tasks/task_rpi.su ./Core/Src/tasks/task_sdlog.cyclo ./Core/Src/tasks/task_sdlog.d ./Core/Src/tasks/task_sdlog.o ./Core/Src/tasks/task_sdlog.su ./Core/Src/tasks/task_sensors.cyclo ./Core/Src/tasks/task_sensors.d ./Core/Src/tasks/task_sensors.o ./Core/Src/tasks/task_sensors.su

.PHONY: clean-Core-2f-Src-2f-tasks

