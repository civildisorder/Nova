################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
CPP_SRCS += \
../src/Config.cpp \
../src/Evidence.cpp \
../src/EvidenceQueue.cpp \
../src/FeatureSet.cpp \
../src/Logger.cpp \
../src/NovaUtil.cpp \
../src/Point.cpp \
../src/Suspect.cpp 

OBJS += \
./src/Config.o \
./src/Evidence.o \
./src/EvidenceQueue.o \
./src/FeatureSet.o \
./src/Logger.o \
./src/NovaUtil.o \
./src/Point.o \
./src/Suspect.o 

CPP_DEPS += \
./src/Config.d \
./src/Evidence.d \
./src/EvidenceQueue.d \
./src/FeatureSet.d \
./src/Logger.d \
./src/NovaUtil.d \
./src/Point.d \
./src/Suspect.d 


# Each subdirectory must supply rules for building sources it contributes
src/%.o: ../src/%.cpp
	@echo 'Building file: $<'
	@echo 'Invoking: GCC C++ Compiler'
	g++ -O0 -g3 -Wall -c -fmessage-length=0  `pkg-config --libs --cflags libnotify` -pthread -std=c++0x -fPIC -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@:%.o=%.d)" -o"$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


