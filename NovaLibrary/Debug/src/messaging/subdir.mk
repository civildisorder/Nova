################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
CPP_SRCS += \
../src/messaging/MessageManager.cpp \
../src/messaging/MessageQueue.cpp 

OBJS += \
./src/messaging/MessageManager.o \
./src/messaging/MessageQueue.o 

CPP_DEPS += \
./src/messaging/MessageManager.d \
./src/messaging/MessageQueue.d 


# Each subdirectory must supply rules for building sources it contributes
src/messaging/%.o: ../src/messaging/%.cpp
	@echo 'Building file: $<'
	@echo 'Invoking: GCC C++ Compiler'
	g++ -O0 -g3 -Wall -c -fmessage-length=0  `pkg-config --libs --cflags libnotify` -pthread -std=c++0x -fPIC -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@:%.o=%.d)" -o"$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


